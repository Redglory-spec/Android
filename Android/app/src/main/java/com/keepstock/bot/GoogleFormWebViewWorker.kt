package com.keepstock.bot

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Bitmap
import android.os.Handler
import android.os.Looper
import android.view.ViewGroup
import android.webkit.CookieManager
import android.webkit.WebChromeClient
import android.webkit.WebResourceError
import android.webkit.WebResourceRequest
import android.webkit.WebSettings
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.LinearLayout
import org.json.JSONObject
import java.util.Collections
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger

class GoogleFormAutomationCoordinator(
    private val context: Context,
    private val webViewContainer: LinearLayout,
    private val onLog: (String, String) -> Unit,
    private val onStatusChange: (String, Int) -> Unit,
    private val onEtaChange: (String) -> Unit,
    private val onProgressChange: (Int, Int) -> Unit,
    private val onRowStatusChange: (Int, String, Boolean) -> Unit,
    private val onFinished: (Boolean, Boolean, Int, Int, Int, List<Int>, String) -> Unit
) {
    private val mainHandler = Handler(Looper.getMainLooper())
    val isRunning = AtomicBoolean(false)
    val isPaused = AtomicBoolean(false)
    val isStopped = AtomicBoolean(false)

    private val activeWebViews = ArrayList<WebView>()

    fun stopAll() {
        isStopped.set(true)
        isPaused.set(false)
    }

    fun cleanupWebViews() {
        mainHandler.post {
            for (wv in activeWebViews) {
                try {
                    wv.stopLoading()
                    (wv.parent as? ViewGroup)?.removeView(wv)
                    wv.destroy()
                } catch (_: Exception) {
                }
            }
            activeWebViews.clear()
        }
    }

    private fun dpToPx(dp: Int): Int {
        val density = context.resources.displayMetrics.density
        return (dp * density + 0.5f).toInt()
    }

    @SuppressLint("SetJavaScriptEnabled")
    private fun createConfiguredWebView(wPrefix: String): WebView {
        val wv = WebView(context)
        val lp = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dpToPx(320))
        lp.bottomMargin = dpToPx(6)
        wv.layoutParams = lp

        val ws = wv.settings
        ws.javaScriptEnabled = true
        ws.domStorageEnabled = true
        ws.databaseEnabled = true
        ws.loadsImagesAutomatically = true
        ws.cacheMode = WebSettings.LOAD_DEFAULT
        ws.userAgentString =
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36"

        CookieManager.getInstance().setAcceptCookie(true)
        CookieManager.getInstance().setAcceptThirdPartyCookies(wv, true)

        wv.webViewClient = object : WebViewClient() {
            override fun onPageStarted(view: WebView?, url: String?, favicon: Bitmap?) {
                onLog("${wPrefix}Memuat halaman...", "INFO")
            }

            override fun onPageFinished(view: WebView?, url: String?) {
                onLog("${wPrefix}Halaman selesai dimuat.", "INFO")
            }

            override fun onReceivedError(view: WebView?, request: WebResourceRequest?, error: WebResourceError?) {
                if (request?.isForMainFrame == true) {
                    onLog("${wPrefix}Error jaringan: ${error?.description}", "ERROR")
                }
            }
        }
        wv.webChromeClient = WebChromeClient()

        webViewContainer.addView(wv)
        activeWebViews.add(wv)
        return wv
    }

    fun startAutomation(
        rows: List<StockRow>,
        targetIndices: List<Int>,
        linkForm: String,
        namaIdentitas: String,
        posisiIdentitas: String,
        headlessMode: Boolean,
        numWorkers: Int,
        maxRetry: Int
    ) {
        if (isRunning.getAndSet(true)) return
        isPaused.set(false)
        isStopped.set(false)

        val totalData = rows.size
        val totalTarget = targetIndices.size
        val actualWorkers = numWorkers.coerceIn(1, minOf(4, maxOf(1, totalTarget)))

        val nextQueuePos = AtomicInteger(0)
        val completedCount = AtomicInteger(0)
        val successCount = AtomicInteger(0)
        val lastSuccessRow = AtomicInteger(0)
        val finishedWorkers = AtomicInteger(0)
        val failedRows = Collections.synchronizedList(ArrayList<Int>())
        val startTimeMs = System.currentTimeMillis()

        val modeTeks = if (headlessMode) "Background/Headless" else "Tampil Layar"
        onLog("Menyiapkan $actualWorkers WebView [$modeTeks] | Target: $totalTarget baris | Maks Retry: ${maxRetry}x...", "INFO")
        onProgressChange(0, totalTarget * 2)
        onEtaChange("Target: $totalTarget baris ($actualWorkers worker) | Estimasi: Menghitung...")

        mainHandler.post {
            for (wv in activeWebViews) {
                try {
                    (wv.parent as? ViewGroup)?.removeView(wv)
                    wv.destroy()
                } catch (_: Exception) {
                }
            }
            activeWebViews.clear()

            for (wId in 1..actualWorkers) {
                val prefix = if (actualWorkers > 1) "[W$wId] " else ""
                val wv = createConfiguredWebView(prefix)
                val worker = SingleWebViewWorker(
                    workerId = wId,
                    totalWorkers = actualWorkers,
                    webView = wv,
                    rows = rows,
                    targetIndices = targetIndices,
                    linkForm = linkForm,
                    namaIdentitas = namaIdentitas,
                    posisiIdentitas = posisiIdentitas,
                    maxRetry = maxRetry,
                    nextQueuePos = nextQueuePos,
                    completedCount = completedCount,
                    successCount = successCount,
                    lastSuccessRow = lastSuccessRow,
                    failedRows = failedRows,
                    sessionStartMs = startTimeMs
                ) {
                    if (finishedWorkers.incrementAndGet() >= actualWorkers) {
                        isRunning.set(false)
                        onFinished(
                            isStopped.get(), false, lastSuccessRow.get(), successCount.get(),
                            totalData, failedRows.toList().sorted(), ""
                        )
                    }
                }
                mainHandler.postDelayed({ worker.processNextRow() }, ((wId - 1) * 400).toLong())
            }
        }
    }

    // =====================================================================
    // WORKER BERBASIS STATE MACHINE + WATCHDOG
    // (Tahan terhadap callback evaluateJavascript yang hilang saat navigasi)
    // =====================================================================
    private enum class Step { PAGE1, NEXT, PAGE2, CONFIRM }

    private inner class SingleWebViewWorker(
        private val workerId: Int,
        private val totalWorkers: Int,
        private val webView: WebView,
        private val rows: List<StockRow>,
        private val targetIndices: List<Int>,
        private val linkForm: String,
        private val namaIdentitas: String,
        private val posisiIdentitas: String,
        private val maxRetry: Int,
        private val nextQueuePos: AtomicInteger,
        private val completedCount: AtomicInteger,
        private val successCount: AtomicInteger,
        private val lastSuccessRow: AtomicInteger,
        private val failedRows: MutableList<Int>,
        private val sessionStartMs: Long,
        private val onWorkerDone: () -> Unit
    ) {
        private val wPrefix = if (totalWorkers > 1) "[W$workerId] " else ""
        private var needInitialLoad = true
        private var finished = false

        private var currentIndex = -1
        private var currentRowNumber = 0
        private var currentAttempt = 1
        private var rowStartMs = 0L

        private var step = Step.PAGE1
        private var stepStartMs = 0L
        private var evalPending = false
        private var evalSentAt = 0L
        private var evalGeneration = 0
        private var lastResult = ""
        private var active = false

        private val tickRunnable = Runnable { tick() }

        private fun scheduleTick(delayMs: Long) {
            mainHandler.removeCallbacks(tickRunnable)
            mainHandler.postDelayed(tickRunnable, delayMs)
        }

        private fun finishWorker() {
            if (finished) return
            finished = true
            active = false
            mainHandler.removeCallbacks(tickRunnable)
            onWorkerDone()
        }

        private fun goTo(newStep: Step) {
            step = newStep
            stepStartMs = System.currentTimeMillis()
            evalPending = false
            evalGeneration++
            lastResult = ""
        }

        private fun stepTimeoutMs(): Long = when (step) {
            Step.PAGE1 -> 30000L
            Step.NEXT -> 15000L
            Step.PAGE2 -> 30000L
            Step.CONFIRM -> 30000L
        }

        fun processNextRow() {
            if (isStopped.get()) { finishWorker(); return }
            if (isPaused.get()) {
                onStatusChange("Dijeda. Ketuk 'LANJUT' untuk meneruskan...", 0xFFF59E0B.toInt())
                mainHandler.postDelayed({ processNextRow() }, 300)
                return
            }

            val qPos = nextQueuePos.getAndIncrement()
            if (qPos >= targetIndices.size) { finishWorker(); return }

            currentIndex = targetIndices[qPos]
            currentRowNumber = currentIndex + 1
            currentAttempt = 1
            rowStartMs = System.currentTimeMillis()
            startAttempt()
        }

        private fun startAttempt() {
            if (isStopped.get()) { finishWorker(); return }

            val skuClean = DataValidator.bersihkanAngka(rows[currentIndex].sku)
            if (currentAttempt > 1) {
                onRowStatusChange(currentIndex, "${wPrefix}Retry $currentAttempt/$maxRetry...", true)
                onStatusChange("${wPrefix}Mencoba ulang baris $currentRowNumber ($currentAttempt/$maxRetry)...", 0xFFF59E0B.toInt())
                needInitialLoad = true
            } else {
                onRowStatusChange(currentIndex, if (totalWorkers > 1) "Memproses [W$workerId]..." else "Memproses...", true)
                onStatusChange("${wPrefix}Memproses baris $currentRowNumber/${rows.size} (SKU: $skuClean)...", 0xFF3B82F6.toInt())
            }

            goTo(Step.PAGE1)
            active = true
            if (needInitialLoad) {
                onLog("${wPrefix}Membuka link Google Form baris $currentRowNumber...", "INFO")
                webView.loadUrl(linkForm)
                scheduleTick(900)
            } else {
                scheduleTick(300)
            }
        }

        // Loop watchdog: berjalan terus setiap ±250ms terlepas dari callback JS
        private fun tick() {
            if (!active) return
            if (isStopped.get()) { finishWorker(); return }
            if (isPaused.get()) { scheduleTick(300); return }

            val now = System.currentTimeMillis()

            if (now - stepStartMs > stepTimeoutMs()) {
                val info = if (lastResult.isNotEmpty()) " [Terakhir: $lastResult]" else " [Tidak ada respon dari halaman]"
                handleAttemptError("Timeout pada tahap ${step.name}.$info")
                return
            }

            // Jika callback sebelumnya hilang (karena navigasi), kirim ulang setelah 1.5 detik
            if (evalPending && now - evalSentAt < 1500L) {
                scheduleTick(250)
                return
            }

            val js = when (step) {
                Step.PAGE1 -> buildJsPage1()
                Step.NEXT -> JS_CLICK_NEXT
                Step.PAGE2 -> buildJsPage2()
                Step.CONFIRM -> JS_CHECK_DONE
            }

            evalPending = true
            evalSentAt = now
            val gen = evalGeneration
            val stepAtSend = step

            try {
                webView.evaluateJavascript(js) { raw ->
                    if (gen != evalGeneration || stepAtSend != step || !active) return@evaluateJavascript
                    evalPending = false
                    val res = (raw ?: "").trim().trim('"')
                    lastResult = res
                    handleResult(stepAtSend, res)
                }
            } catch (e: Exception) {
                evalPending = false
            }

            scheduleTick(300)
        }

        private fun handleResult(s: Step, res: String) {
            when (s) {
                Step.PAGE1 -> when {
                    res == "POSISI_OK" -> {
                        onLog("${wPrefix}Identitas PIC ($namaIdentitas - $posisiIdentitas) terisi.", "INFO")
                        goTo(Step.NEXT)
                        scheduleTick(250)
                    }
                    res.startsWith("ERR") -> {
                        onLog("${wPrefix}Diagnosa Posisi: $res", "WARN")
                    }
                }
                Step.NEXT -> if (res == "CLICKED_NEXT") {
                    onLog("${wPrefix}Klik 'Berikutnya' -> menuju halaman data stok...", "INFO")
                    onProgressChange(completedCount.get() * 2 + 1, targetIndices.size * 2)
                    goTo(Step.PAGE2)
                    scheduleTick(600)
                }
                Step.PAGE2 -> if (res == "SUBMITTED") {
                    onLog("${wPrefix}4 kolom stok terisi. Mengirim formulir...", "INFO")
                    goTo(Step.CONFIRM)
                    scheduleTick(600)
                }
                Step.CONFIRM -> when (res) {
                    "DONE_AGAIN" -> { needInitialLoad = false; markRowSuccess() }
                    "DONE" -> { needInitialLoad = true; markRowSuccess() }
                }
            }
        }

        private fun buildJsPage1(): String {
            val nama = JSONObject.quote(namaIdentitas)
            val posisi = JSONObject.quote(posisiIdentitas)
            return """
                (function() {
                    try {
                        $JS_HELPERS
                        var inputs = visibleTextInputs();
                        var dd = document.querySelector('div[role="listbox"]');
                        if (inputs.length < 1 || !dd) return 'WAIT_PAGE1|in=' + inputs.length;

                        var target = $nama;
                        if (inputs[0].value !== target) setVal(inputs[0], target);

                        var want = ($posisi || '').toUpperCase();
                        var headerBox = dd.querySelector('[jsname="d9BH4c"]') || dd.querySelector('[jsname="LgbsSe"]');
                        var sel = headerBox ? headerBox.querySelector('div[role="option"][aria-selected="true"]')
                                            : dd.querySelector('div[role="option"][aria-selected="true"]');
                        var selVal = sel ? (sel.getAttribute('data-value') || (sel.innerText || '').trim()) : '';
                        if (selVal.toUpperCase() === want) return 'POSISI_OK';

                        // Cari opsi di dalam kontainer popup Google Closure [jsname="V68bde"]
                        var popup = dd.querySelector('[jsname="V68bde"]') || document;
                        var popupOpts = popup.querySelectorAll('div[role="option"][jsname="wQNmvb"], div[role="option"]');
                        var targetOpt = null;
                        var visPopupCount = 0;
                        for (var i = 0; i < popupOpts.length; i++) {
                            var o = popupOpts[i];
                            if (headerBox && headerBox.contains(o)) continue;
                            var r = o.getBoundingClientRect();
                            if (r.width > 0 && r.height > 0) {
                                visPopupCount++;
                                var dv = (o.getAttribute('data-value') || (o.innerText || '').trim()).toUpperCase();
                                if (dv === want) targetOpt = o;
                            }
                        }

                        var isExpanded = (dd.getAttribute('aria-expanded') === 'true') || (visPopupCount > 0);
                        if (!isExpanded) {
                            // Wajib klik [jsname="LgbsSe"] sesuai jsaction="click:cOuCgd(LgbsSe)"
                            var trigger = dd.querySelector('[jsname="LgbsSe"]') || dd;
                            tapEl(trigger);
                            return 'OPENING_DD|exp=' + dd.getAttribute('aria-expanded') + '|vis=' + visPopupCount;
                        }

                        if (targetOpt) {
                            tapEl(targetOpt);
                            return 'PICKED_OPT|' + want + '|vis=' + visPopupCount;
                        }

                        return 'WAIT_POPUP_OPTS|vis=' + visPopupCount;
                    } catch (e) { return 'ERR|' + e.message; }
                })();
            """.trimIndent()
        }

        private fun buildJsPage2(): String {
            val row = rows[currentIndex]
            val vKeep = JSONObject.quote(DataValidator.bersihkanAngka(row.keepStock))
            val vSku = JSONObject.quote(DataValidator.bersihkanAngka(row.sku))
            val vQty = JSONObject.quote(DataValidator.bersihkanAngka(row.skuQty))
            val vGon = JSONObject.quote(DataValidator.bersihkanAngka(row.gondola))
            return """
                (function() {
                    try {
                        $JS_HELPERS
                        var inputs = visibleTextInputs();
                        var btn = findButton(['Kirim', 'Submit']);
                        if (inputs.length < 4 || !btn) return 'WAIT_PAGE2|in=' + inputs.length + '|btn=' + (!!btn);
                        var vals = [$vKeep, $vSku, $vQty, $vGon];
                        for (var i = 0; i < 4; i++) {
                            if (inputs[i].value !== vals[i]) setVal(inputs[i], vals[i]);
                        }
                        if (inputs[0].value !== vals[0] || inputs[3].value !== vals[3]) return 'FILLING';
                        tapEl(btn);
                        return 'SUBMITTED';
                    } catch (e) { return 'ERR|' + e.message; }
                })();
            """.trimIndent()
        }

        private fun markRowSuccess() {
            active = false
            mainHandler.removeCallbacks(tickRunnable)

            val durSec = (System.currentTimeMillis() - rowStartMs) / 1000.0
            val durStr = String.format("%.1fs", durSec)
            successCount.incrementAndGet()
            val doneNow = completedCount.incrementAndGet()

            var prevLast = lastSuccessRow.get()
            while (currentRowNumber > prevLast && !lastSuccessRow.compareAndSet(prevLast, currentRowNumber)) {
                prevLast = lastSuccessRow.get()
            }

            onRowStatusChange(currentIndex, "✓ Sukses ($durStr)", false)
            onProgressChange(doneNow * 2, targetIndices.size * 2)

            val elapsedSec = maxOf(0.1, (System.currentTimeMillis() - sessionStartMs) / 1000.0)
            val etaSec = (maxOf(0, targetIndices.size - doneNow) * (elapsedSec / doneNow)).toInt()
            val etaFmt = if (etaSec >= 60) "${etaSec / 60}m ${etaSec % 60}d" else "$etaSec detik"
            onEtaChange("Selesai: $doneNow/${targetIndices.size} | Sisa waktu: ± $etaFmt")

            val row = rows[currentIndex]
            onLog(
                "${wPrefix}Baris $currentRowNumber SUKSES -> SKU: ${DataValidator.bersihkanAngka(row.sku)} | QTY: ${DataValidator.bersihkanAngka(row.skuQty)} ($durStr)",
                "SUKSES"
            )
            mainHandler.postDelayed({ processNextRow() }, 150)
        }

        private fun handleAttemptError(reason: String) {
            active = false
            mainHandler.removeCallbacks(tickRunnable)
            if (isStopped.get()) { finishWorker(); return }

            onLog("${wPrefix}Gagal baris $currentRowNumber (Percobaan $currentAttempt/$maxRetry): $reason", "ERROR")

            if (currentAttempt < maxRetry) {
                currentAttempt++
                needInitialLoad = true
                mainHandler.postDelayed({ startAttempt() }, 1200)
            } else {
                val doneNow = completedCount.incrementAndGet()
                failedRows.add(currentIndex)
                onRowStatusChange(currentIndex, "✗ Gagal (${maxRetry}x)", false)
                onProgressChange(doneNow * 2, targetIndices.size * 2)
                onLog("${wPrefix}Baris $currentRowNumber DILEWATI setelah gagal $maxRetry kali.", "WARN")
                needInitialLoad = true
                mainHandler.postDelayed({ processNextRow() }, 300)
            }
        }
    }

    companion object {
        // Fungsi bantu JS yang telah diverifikasi via simulasi lokal terhadap Google Closure Wiz
        private const val JS_HELPERS = """
            function isVis(el) {
                if (!el) return false;
                var r = el.getBoundingClientRect();
                var st = window.getComputedStyle(el);
                return r.width > 0 && r.height > 0 && st.visibility !== 'hidden' && st.display !== 'none';
            }
            function visibleTextInputs() {
                var all = document.querySelectorAll('input[type="text"], input[type="number"], input:not([type]), textarea');
                var out = [];
                for (var i = 0; i < all.length; i++) {
                    var t = (all[i].getAttribute('type') || '').toLowerCase();
                    if (t === 'hidden' || t === 'checkbox' || t === 'radio') continue;
                    if (all[i].offsetParent !== null || isVis(all[i])) out.push(all[i]);
                }
                return out;
            }
            function setVal(el, val) {
                el.focus();
                var proto = el.tagName === 'TEXTAREA' ? window.HTMLTextAreaElement.prototype : window.HTMLInputElement.prototype;
                var setter = Object.getOwnPropertyDescriptor(proto, 'value').set;
                if (setter) setter.call(el, val);
                else el.value = val;
                el.dispatchEvent(new Event('input', { bubbles: true }));
                el.dispatchEvent(new Event('change', { bubbles: true }));
                el.blur();
            }
            function tapEl(el) {
                if (!el) return;
                var r = el.getBoundingClientRect();
                var cx = r.left + (r.width > 0 ? r.width / 2 : 10);
                var cy = r.top + (r.height > 0 ? r.height / 2 : 10);
                ['mousedown', 'mouseup', 'click'].forEach(function(t) {
                    el.dispatchEvent(new MouseEvent(t, {
                        view: window, bubbles: true, cancelable: true,
                        clientX: cx, clientY: cy, button: 0, buttons: 1
                    }));
                });
            }
            function findButton(labels) {
                var btns = document.querySelectorAll('div[role="button"], span[role="button"], button');
                for (var i = 0; i < btns.length; i++) {
                    var tx = (btns[i].innerText || btns[i].textContent || '').trim().toLowerCase();
                    for (var j = 0; j < labels.length; j++) {
                        if (tx.indexOf(labels[j].toLowerCase()) !== -1) {
                            var b = btns[i].closest('div[role="button"]') || btns[i];
                            if (isVis(b)) return b;
                        }
                    }
                }
                return null;
            }
        """

        private val JS_CLICK_NEXT = """
            (function() {
                try {
                    $JS_HELPERS
                    var btn = findButton(['Berikutnya', 'Next']);
                    if (!btn) return 'WAIT_NEXT';
                    tapEl(btn);
                    return 'CLICKED_NEXT';
                } catch (e) { return 'ERR|' + e.message; }
            })();
        """.trimIndent()

        private val JS_CHECK_DONE = """
            (function() {
                try {
                    var body = document.body ? (document.body.innerText || '') : '';
                    var done = body.indexOf('telah direkam') !== -1 ||
                               body.indexOf('has been recorded') !== -1 ||
                               location.href.indexOf('formResponse') !== -1;
                    if (!done) return 'WAIT_CONFIRM';
                    var links = document.querySelectorAll('a[href*="viewform"]');
                    if (links.length > 0) { links[0].click(); return 'DONE_AGAIN'; }
                    return 'DONE';
                } catch (e) { return 'ERR|' + e.message; }
            })();
        """.trimIndent()
    }
}
