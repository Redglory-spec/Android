package com.keepstock.bot

import android.annotation.SuppressLint
import android.content.Context
import android.os.Handler
import android.os.Looper
import android.view.ViewGroup
import android.webkit.CookieManager
import android.webkit.WebChromeClient
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

    @SuppressLint("SetJavaScriptEnabled")
    private fun createConfiguredWebView(workerId: Int): WebView {
        val wv = WebView(context)
        val lp = LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            dpToPx(240)
        )
        lp.bottomMargin = dpToPx(6)
        wv.layoutParams = lp

        val ws = wv.settings
        ws.javaScriptEnabled = true
        ws.domStorageEnabled = true
        ws.databaseEnabled = true
        ws.loadsImagesAutomatically = false // Percepat loading form dengan tidak memuat gambar dekorasi
        ws.cacheMode = WebSettings.LOAD_DEFAULT
        // Gunakan User-Agent Desktop Chrome agar DOM & XPath Google Form identik 100% dengan versi PC
        ws.userAgentString =
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36"

        CookieManager.getInstance().setAcceptCookie(true)
        CookieManager.getInstance().setAcceptThirdPartyCookies(wv, true)

        wv.webViewClient = object : WebViewClient() {}
        wv.webChromeClient = WebChromeClient()

        webViewContainer.addView(wv)
        activeWebViews.add(wv)
        return wv
    }

    private fun dpToPx(dp: Int): Int {
        val density = context.resources.displayMetrics.density
        return (dp * density + 0.5f).toInt()
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

        val modeTeks = if (headlessMode) "Background/Headless" else "Tampil WebView"
        onLog(
            "Menyiapkan $actualWorkers WebView [$modeTeks] | Target: $totalTarget baris | Maks Retry: ${maxRetry}x...",
            "INFO"
        )
        onProgressChange(0, totalTarget * 2)
        onEtaChange("Target: $totalTarget baris ($actualWorkers worker) | Estimasi: Menghitung...")

        mainHandler.post {
            cleanupWebViews()
            for (wId in 1..actualWorkers) {
                val wv = createConfiguredWebView(wId)
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
                        val sortedFailed = failedRows.toList().sorted()
                        onFinished(
                            isStopped.get(),
                            false,
                            lastSuccessRow.get(),
                            successCount.get(),
                            totalData,
                            sortedFailed,
                            ""
                        )
                    }
                }
                mainHandler.postDelayed({ worker.processNextRow() }, ((wId - 1) * 300).toLong())
            }
        }
    }

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

        private var currentIndex = -1
        private var currentRowNumber = 0
        private var currentAttempt = 1
        private var rowStartMs = 0L

        fun processNextRow() {
            if (isStopped.get()) {
                onWorkerDone()
                return
            }

            if (isPaused.get()) {
                onStatusChange("Dijeda. Ketuk 'LANJUT' untuk meneruskan...", 0xFFF59E0B.toInt())
                mainHandler.postDelayed({ processNextRow() }, 250)
                return
            }

            val qPos = nextQueuePos.getAndIncrement()
            if (qPos >= targetIndices.size) {
                onWorkerDone()
                return
            }

            currentIndex = targetIndices[qPos]
            currentRowNumber = currentIndex + 1
            currentAttempt = 1
            rowStartMs = System.currentTimeMillis()

            startAttempt()
        }

        private fun startAttempt() {
            if (isStopped.get()) {
                onWorkerDone()
                return
            }
            if (isPaused.get()) {
                mainHandler.postDelayed({ startAttempt() }, 250)
                return
            }

            val row = rows[currentIndex]
            val skuClean = DataValidator.bersihkanAngka(row.sku)

            if (currentAttempt > 1) {
                onRowStatusChange(currentIndex, "${wPrefix}Retry $currentAttempt/$maxRetry...", true)
                onStatusChange(
                    "${wPrefix}Mencoba ulang baris $currentRowNumber ($currentAttempt/$maxRetry)...",
                    0xFFF59E0B.toInt()
                )
                needInitialLoad = true
            } else {
                val procLabel = if (totalWorkers > 1) "Memproses [W$workerId]..." else "Memproses..."
                onRowStatusChange(currentIndex, procLabel, true)
                onStatusChange(
                    "${wPrefix}Memproses baris $currentRowNumber/${rows.size} (SKU: $skuClean)...",
                    0xFF3B82F6.toInt()
                )
            }

            if (needInitialLoad) {
                webView.loadUrl(linkForm)
            }

            waitForLayer1AndFill(System.currentTimeMillis())
        }

        private fun waitForLayer1AndFill(stepStartMs: Long) {
            if (isStopped.get()) {
                onWorkerDone()
                return
            }
            if (System.currentTimeMillis() - stepStartMs > 18000L) {
                handleAttemptError("Timeout menunggu halaman 1 (Identitas PIC) terbuka.")
                return
            }

            val escapedNama = JSONObject.quote(namaIdentitas)
            val escapedPosisi = JSONObject.quote(posisiIdentitas)

            val jsLayer1 = """
                (function() {
                    function byXPath(xp) {
                        return document.evaluate(xp, document, null, XPathResult.FIRST_ORDERED_NODE_TYPE, null).singleNodeValue;
                    }
                    function isVisible(el) {
                        if (!el) return false;
                        var r = el.getBoundingClientRect();
                        return r.width > 0 && r.height > 0;
                    }
                    function setInputValue(el, val) {
                        el.focus();
                        var setter = Object.getOwnPropertyDescriptor(window.HTMLInputElement.prototype, 'value').set;
                        if (setter) setter.call(el, val); else el.value = val;
                        el.dispatchEvent(new Event('input', { bubbles: true }));
                        el.dispatchEvent(new Event('change', { bubbles: true }));
                        el.dispatchEvent(new Event('blur', { bubbles: true }));
                    }
                    function fullClick(el) {
                        if (!el) return;
                        ['mousedown', 'mouseup', 'click'].forEach(function(evtType) {
                            el.dispatchEvent(new MouseEvent(evtType, { bubbles: true, cancelable: true, view: window }));
                        });
                    }

                    var inpNama = byXPath('(//input[@type="text"])[1]');
                    var ddPosisi = byXPath("(//div[@role='listbox'])[1]");
                    var btnNext = byXPath("//span[text()='Berikutnya' or text()='Next']");

                    if (!isVisible(inpNama) || !isVisible(ddPosisi) || !isVisible(btnNext)) {
                        return 'WAIT_PAGE1';
                    }

                    if (inpNama.value !== $escapedNama) {
                        setInputValue(inpNama, $escapedNama);
                    }

                    var optPosisi = byXPath("//div[@role='option']//span[text()=" + $escapedPosisi + "]");
                    if (!isVisible(optPosisi)) {
                        fullClick(ddPosisi);
                        return 'OPENING_DROPDOWN';
                    }

                    var optContainer = optPosisi.closest("div[role='option']") || optPosisi;
                    fullClick(optContainer);
                    return 'POSISI_CLICKED';
                })();
            """.trimIndent()

            webView.evaluateJavascript(jsLayer1) { rawRes ->
                val res = rawRes?.trim('"', ' ') ?: ""
                when (res) {
                    "POSISI_CLICKED" -> {
                        mainHandler.postDelayed({ clickNextButton(System.currentTimeMillis()) }, 180)
                    }
                    else -> {
                        mainHandler.postDelayed({ waitForLayer1AndFill(stepStartMs) }, 140)
                    }
                }
            }
        }

        private fun clickNextButton(stepStartMs: Long) {
            if (isStopped.get()) {
                onWorkerDone()
                return
            }
            if (System.currentTimeMillis() - stepStartMs > 10000L) {
                handleAttemptError("Timeout menekan tombol Berikutnya.")
                return
            }

            val jsClickNext = """
                (function() {
                    function byXPath(xp) {
                        return document.evaluate(xp, document, null, XPathResult.FIRST_ORDERED_NODE_TYPE, null).singleNodeValue;
                    }
                    var spanNext = byXPath("//span[text()='Berikutnya' or text()='Next']");
                    if (!spanNext) return 'WAIT';
                    var btn = spanNext.closest("div[role='button']") || spanNext;
                    ['mousedown', 'mouseup', 'click'].forEach(function(evtType) {
                        btn.dispatchEvent(new MouseEvent(evtType, { bubbles: true, cancelable: true, view: window }));
                    });
                    return 'CLICKED_NEXT';
                })();
            """.trimIndent()

            webView.evaluateJavascript(jsClickNext) { rawRes ->
                val res = rawRes?.trim('"', ' ') ?: ""
                if (res == "CLICKED_NEXT") {
                    val doneSoFar = completedCount.get()
                    onProgressChange(doneSoFar * 2 + 1, targetIndices.size * 2)
                    mainHandler.postDelayed({ waitForLayer2AndSubmit(System.currentTimeMillis()) }, 180)
                } else {
                    mainHandler.postDelayed({ clickNextButton(stepStartMs) }, 140)
                }
            }
        }

        private fun waitForLayer2AndSubmit(stepStartMs: Long) {
            if (isStopped.get()) {
                onWorkerDone()
                return
            }
            if (System.currentTimeMillis() - stepStartMs > 18000L) {
                handleAttemptError("Timeout menunggu halaman 2 (4 Kolom Stok) terbuka.")
                return
            }

            val row = rows[currentIndex]
            val vKeep = JSONObject.quote(DataValidator.bersihkanAngka(row.keepStock))
            val vSku = JSONObject.quote(DataValidator.bersihkanAngka(row.sku))
            val vQty = JSONObject.quote(DataValidator.bersihkanAngka(row.skuQty))
            val vGon = JSONObject.quote(DataValidator.bersihkanAngka(row.gondola))

            val jsLayer2 = """
                (function() {
                    function byXPath(xp) {
                        return document.evaluate(xp, document, null, XPathResult.FIRST_ORDERED_NODE_TYPE, null).singleNodeValue;
                    }
                    function isVisible(el) {
                        if (!el) return false;
                        var r = el.getBoundingClientRect();
                        return r.width > 0 && r.height > 0;
                    }
                    function setInputValue(el, val) {
                        el.focus();
                        var setter = Object.getOwnPropertyDescriptor(window.HTMLInputElement.prototype, 'value').set;
                        if (setter) setter.call(el, val); else el.value = val;
                        el.dispatchEvent(new Event('input', { bubbles: true }));
                        el.dispatchEvent(new Event('change', { bubbles: true }));
                        el.dispatchEvent(new Event('blur', { bubbles: true }));
                    }

                    var i1 = byXPath('(//input[@type="text"])[1]');
                    var i2 = byXPath('(//input[@type="text"])[2]');
                    var i3 = byXPath('(//input[@type="text"])[3]');
                    var i4 = byXPath('(//input[@type="text"])[4]');
                    var spanKirim = byXPath("//span[text()='Kirim' or text()='Submit']");

                    if (!isVisible(i1) || !isVisible(i2) || !isVisible(i3) || !isVisible(i4) || !isVisible(spanKirim)) {
                        return 'WAIT_PAGE2';
                    }

                    setInputValue(i1, $vKeep);
                    setInputValue(i2, $vSku);
                    setInputValue(i3, $vQty);
                    setInputValue(i4, $vGon);

                    var btnSubmit = spanKirim.closest("div[role='button']") || spanKirim;
                    ['mousedown', 'mouseup', 'click'].forEach(function(evtType) {
                        btnSubmit.dispatchEvent(new MouseEvent(evtType, { bubbles: true, cancelable: true, view: window }));
                    });
                    return 'SUBMITTED';
                })();
            """.trimIndent()

            webView.evaluateJavascript(jsLayer2) { rawRes ->
                val res = rawRes?.trim('"', ' ') ?: ""
                if (res == "SUBMITTED") {
                    mainHandler.postDelayed({ waitForSubmitConfirm(System.currentTimeMillis()) }, 180)
                } else {
                    mainHandler.postDelayed({ waitForLayer2AndSubmit(stepStartMs) }, 140)
                }
            }
        }

        private fun waitForSubmitConfirm(stepStartMs: Long) {
            if (isStopped.get()) {
                onWorkerDone()
                return
            }
            if (System.currentTimeMillis() - stepStartMs > 18000L) {
                handleAttemptError("Timeout menunggu konfirmasi pengiriman form.")
                return
            }

            val jsCheckDone = """
                (function() {
                    function byXPath(xp) {
                        return document.evaluate(xp, document, null, XPathResult.FIRST_ORDERED_NODE_TYPE, null).singleNodeValue;
                    }
                    var linkAgain = byXPath("//a[contains(@href, 'viewform') or contains(@href, 'form_confirm')]");
                    if (linkAgain) {
                        linkAgain.click();
                        return 'DONE_CLICKED_AGAIN';
                    }
                    var spanKirim = byXPath("//span[text()='Kirim' or text()='Submit']");
                    if (!spanKirim) {
                        return 'DONE_NO_SUBMIT_BTN';
                    }
                    return 'WAIT_CONFIRM';
                })();
            """.trimIndent()

            webView.evaluateJavascript(jsCheckDone) { rawRes ->
                val res = rawRes?.trim('"', ' ') ?: ""
                when (res) {
                    "DONE_CLICKED_AGAIN" -> {
                        needInitialLoad = false
                        markRowSuccess()
                    }
                    "DONE_NO_SUBMIT_BTN" -> {
                        needInitialLoad = true
                        markRowSuccess()
                    }
                    else -> {
                        mainHandler.postDelayed({ waitForSubmitConfirm(stepStartMs) }, 140)
                    }
                }
            }
        }

        private fun markRowSuccess() {
            val durSec = (System.currentTimeMillis() - rowStartMs) / 1000.0
            val durStr = String.format("%.1fs", durSec)

            val totalSucc = successCount.incrementAndGet()
            val doneNow = completedCount.incrementAndGet()

            var prevLast = lastSuccessRow.get()
            while (currentRowNumber > prevLast && !lastSuccessRow.compareAndSet(prevLast, currentRowNumber)) {
                prevLast = lastSuccessRow.get()
            }

            onRowStatusChange(currentIndex, "✓ Sukses ($durStr)", false)
            onProgressChange(doneNow * 2, targetIndices.size * 2)

            val elapsedSec = maxOf(0.1, (System.currentTimeMillis() - sessionStartMs) / 1000.0)
            val avgPerRow = elapsedSec / doneNow
            val remainingRows = maxOf(0, targetIndices.size - doneNow)
            val etaSec = (remainingRows * avgPerRow).toInt()
            val etaFormatted = if (etaSec >= 60) {
                "${etaSec / 60}m ${etaSec % 60}d"
            } else {
                "$etaSec detik"
            }

            onEtaChange("Selesai: $doneNow/${targetIndices.size} | Sisa waktu: ± $etaFormatted")

            val row = rows[currentIndex]
            onLog(
                "${wPrefix}Baris $currentRowNumber/${rows.size} BERHASIL -> SKU: ${DataValidator.bersihkanAngka(row.sku)} | QTY: ${DataValidator.bersihkanAngka(row.skuQty)} ($durStr)",
                "SUKSES"
            )

            mainHandler.postDelayed({ processNextRow() }, 80)
        }

        private fun handleAttemptError(reason: String) {
            if (isStopped.get()) {
                onWorkerDone()
                return
            }

            onLog("${wPrefix}Gagal baris $currentRowNumber (Percobaan $currentAttempt/$maxRetry): $reason", "ERROR")

            if (currentAttempt < maxRetry) {
                currentAttempt++
                needInitialLoad = true
                mainHandler.postDelayed({ startAttempt() }, 900)
            } else {
                val doneNow = completedCount.incrementAndGet()
                failedRows.add(currentIndex)
                onRowStatusChange(currentIndex, "✗ Gagal (${maxRetry}x)", false)
                onProgressChange(doneNow * 2, targetIndices.size * 2)
                onLog(
                    "${wPrefix}Baris $currentRowNumber DILEWATI setelah gagal $maxRetry kali. Lanjut ke baris berikutnya...",
                    "WARN"
                )
                needInitialLoad = true
                mainHandler.postDelayed({ processNextRow() }, 200)
            }
        }
    }
}
