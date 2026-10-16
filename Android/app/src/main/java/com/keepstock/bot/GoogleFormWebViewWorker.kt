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
        ws.loadsImagesAutomatically = true
        ws.cacheMode = WebSettings.LOAD_DEFAULT
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

        val modeTeks = if (headlessMode) "Background/Headless" else "Tampil Layar"
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
                onLog("${wPrefix}Membuka link Google Form baris $currentRowNumber...", "INFO")
                webView.loadUrl(linkForm)
            }

            waitForLayer1AndFill(System.currentTimeMillis())
        }

        private fun waitForLayer1AndFill(stepStartMs: Long) {
            if (isStopped.get()) {
                onWorkerDone()
                return
            }
            if (System.currentTimeMillis() - stepStartMs > 25000L) {
                handleAttemptError("Timeout menunggu form terbuka (Koneksi lambat atau link tidak merespon).")
                return
            }

            val escapedNama = JSONObject.quote(namaIdentitas)
            val escapedPosisi = JSONObject.quote(posisiIdentitas)

            val jsLayer1 = """
                (function() {
                    function byXPath(xp) {
                        try {
                            return document.evaluate(xp, document, null, XPathResult.FIRST_ORDERED_NODE_TYPE, null).singleNodeValue;
                        } catch(e) { return null; }
                    }
                    function setInputValue(el, val) {
                        if (!el) return;
                        el.focus();
                        var setter = Object.getOwnPropertyDescriptor(window.HTMLInputElement.prototype, 'value').set;
                        if (setter) { setter.call(el, val); } else { el.value = val; }
                        el.dispatchEvent(new Event('input', { bubbles: true }));
                        el.dispatchEvent(new Event('change', { bubbles: true }));
                        el.dispatchEvent(new Event('blur', { bubbles: true }));
                    }
                    function fullClick(el) {
                        if (!el) return;
                        ['mousedown', 'mouseup', 'click'].forEach(function(evtType) {
                            try {
                                el.dispatchEvent(new MouseEvent(evtType, { bubbles: true, cancelable: true, view: window }));
                            } catch(e) { el.click(); }
                        });
                    }

                    var inputs = document.querySelectorAll('input[type="text"], input:not([type])');
                    var inpNama = inputs.length > 0 ? inputs[0] : byXPath('(//input[@type="text"])[1]');
                    var ddPosisi = document.querySelector('div[role="listbox"]') || byXPath("(//div[@role='listbox'])[1]");

                    if (!inpNama || !ddPosisi) {
                        return 'WAIT_PAGE1';
                    }

                    if (inpNama.value !== $escapedNama) {
                        setInputValue(inpNama, $escapedNama);
                    }

                    // Cari opsi posisi di dropdown
                    var optPosisi = byXPath("//div[@role='option']//span[text()=" + $escapedPosisi + "]") ||
                                    byXPath("//div[@role='option'][contains(., " + $escapedPosisi + ")]");

                    if (!optPosisi) {
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
                        onLog("${wPrefix}Identitas PIC ($namaIdentitas - $posisiIdentitas) terisi. Lanjut ke halaman 2...", "INFO")
                        mainHandler.postDelayed({ clickNextButton(System.currentTimeMillis()) }, 200)
                    }
                    "OPENING_DROPDOWN" -> {
                        mainHandler.postDelayed({ waitForLayer1AndFill(stepStartMs) }, 200)
                    }
                    else -> {
                        mainHandler.postDelayed({ waitForLayer1AndFill(stepStartMs) }, 180)
                    }
                }
            }
        }

        private fun clickNextButton(stepStartMs: Long) {
            if (isStopped.get()) {
                onWorkerDone()
                return
            }
            if (System.currentTimeMillis() - stepStartMs > 12000L) {
                handleAttemptError("Timeout menekan tombol Berikutnya.")
                return
            }

            val jsClickNext = """
                (function() {
                    function byXPath(xp) {
                        try {
                            return document.evaluate(xp, document, null, XPathResult.FIRST_ORDERED_NODE_TYPE, null).singleNodeValue;
                        } catch(e) { return null; }
                    }
                    function fullClick(el) {
                        if (!el) return;
                        ['mousedown', 'mouseup', 'click'].forEach(function(evtType) {
                            try {
                                el.dispatchEvent(new MouseEvent(evtType, { bubbles: true, cancelable: true, view: window }));
                            } catch(e) { el.click(); }
                        });
                    }

                    var spanNext = byXPath("//span[text()='Berikutnya' or text()='Next']") ||
                                   byXPath("//div[@role='button']//span[contains(text(),'Berikutnya') or contains(text(),'Next')]");
                    if (!spanNext) return 'WAIT';
                    var btn = spanNext.closest("div[role='button']") || spanNext;
                    fullClick(btn);
                    return 'CLICKED_NEXT';
                })();
            """.trimIndent()

            webView.evaluateJavascript(jsClickNext) { rawRes ->
                val res = rawRes?.trim('"', ' ') ?: ""
                if (res == "CLICKED_NEXT") {
                    val doneSoFar = completedCount.get()
                    onProgressChange(doneSoFar * 2 + 1, targetIndices.size * 2)
                    mainHandler.postDelayed({ waitForLayer2AndSubmit(System.currentTimeMillis()) }, 250)
                } else {
                    mainHandler.postDelayed({ clickNextButton(stepStartMs) }, 180)
                }
            }
        }

        private fun waitForLayer2AndSubmit(stepStartMs: Long) {
            if (isStopped.get()) {
                onWorkerDone()
                return
            }
            if (System.currentTimeMillis() - stepStartMs > 25000L) {
                handleAttemptError("Timeout menunggu halaman 2 (4 Kolom Stok) muncul.")
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
                        try {
                            return document.evaluate(xp, document, null, XPathResult.FIRST_ORDERED_NODE_TYPE, null).singleNodeValue;
                        } catch(e) { return null; }
                    }
                    function setInputValue(el, val) {
                        if (!el) return;
                        el.focus();
                        var setter = Object.getOwnPropertyDescriptor(window.HTMLInputElement.prototype, 'value').set;
                        if (setter) { setter.call(el, val); } else { el.value = val; }
                        el.dispatchEvent(new Event('input', { bubbles: true }));
                        el.dispatchEvent(new Event('change', { bubbles: true }));
                        el.dispatchEvent(new Event('blur', { bubbles: true }));
                    }
                    function fullClick(el) {
                        if (!el) return;
                        ['mousedown', 'mouseup', 'click'].forEach(function(evtType) {
                            try {
                                el.dispatchEvent(new MouseEvent(evtType, { bubbles: true, cancelable: true, view: window }));
                            } catch(e) { el.click(); }
                        });
                    }

                    var inputs = document.querySelectorAll('input[type="text"], input:not([type])');
                    var spanKirim = byXPath("//span[text()='Kirim' or text()='Submit']") ||
                                    byXPath("//div[@role='button']//span[contains(text(),'Kirim') or contains(text(),'Submit')]");

                    if (inputs.length < 4 || !spanKirim) {
                        return 'WAIT_PAGE2';
                    }

                    setInputValue(inputs[0], $vKeep);
                    setInputValue(inputs[1], $vSku);
                    setInputValue(inputs[2], $vQty);
                    setInputValue(inputs[3], $vGon);

                    var btnSubmit = spanKirim.closest("div[role='button']") || spanKirim;
                    fullClick(btnSubmit);
                    return 'SUBMITTED';
                })();
            """.trimIndent()

            webView.evaluateJavascript(jsLayer2) { rawRes ->
                val res = rawRes?.trim('"', ' ') ?: ""
                if (res == "SUBMITTED") {
                    onLog("${wPrefix}Data stok terisi (SKU: ${DataValidator.bersihkanAngka(row.sku)}). Mengirim formulir...", "INFO")
                    mainHandler.postDelayed({ waitForSubmitConfirm(System.currentTimeMillis()) }, 250)
                } else {
                    mainHandler.postDelayed({ waitForLayer2AndSubmit(stepStartMs) }, 180)
                }
            }
        }

        private fun waitForSubmitConfirm(stepStartMs: Long) {
            if (isStopped.get()) {
                onWorkerDone()
                return
            }
            if (System.currentTimeMillis() - stepStartMs > 25000L) {
                handleAttemptError("Timeout menunggu konfirmasi pengiriman form.")
                return
            }

            val jsCheckDone = """
                (function() {
                    function byXPath(xp) {
                        try {
                            return document.evaluate(xp, document, null, XPathResult.FIRST_ORDERED_NODE_TYPE, null).singleNodeValue;
                        } catch(e) { return null; }
                    }
                    var linkAgain = byXPath("//a[contains(@href, 'viewform') or contains(@href, 'form_confirm') or contains(@href, 'formResponse')]");
                    if (linkAgain) {
                        linkAgain.click();
                        return 'DONE_CLICKED_AGAIN';
                    }
                    var bodyText = document.body ? document.body.innerText : '';
                    if (bodyText.indexOf('Jawaban Anda telah direkam') !== -1 ||
                        bodyText.indexOf('telah direkam') !== -1 ||
                        bodyText.indexOf('response has been recorded') !== -1) {
                        return 'DONE_CONFIRMED';
                    }
                    var spanKirim = byXPath("//span[text()='Kirim' or text()='Submit']");
                    if (!spanKirim && bodyText.length > 50) {
                        return 'DONE_NO_SUBMIT_BTN';
                    }
                    return 'WAIT_CONFIRM';
                })();
            """.trimIndent()

            webView.evaluateJavascript(jsCheckDone) { rawRes ->
                val res = rawRes?.trim('"', ' ') ?: ""
                when {
                    res == "DONE_CLICKED_AGAIN" -> {
                        needInitialLoad = false
                        markRowSuccess()
                    }
                    res == "DONE_CONFIRMED" || res == "DONE_NO_SUBMIT_BTN" -> {
                        needInitialLoad = true
                        markRowSuccess()
                    }
                    else -> {
                        mainHandler.postDelayed({ waitForSubmitConfirm(stepStartMs) }, 200)
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
                "${wPrefix}Baris $currentRowNumber/${rows.size} SUKSES -> SKU: ${DataValidator.bersihkanAngka(row.sku)} | QTY: ${DataValidator.bersihkanAngka(row.skuQty)} ($durStr)",
                "SUKSES"
            )

            mainHandler.postDelayed({ processNextRow() }, 100)
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
                mainHandler.postDelayed({ startAttempt() }, 1000)
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
