package com.keepstock.bot

import android.app.Activity
import android.app.AlertDialog
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.net.Uri
import android.os.Bundle
import android.provider.OpenableColumns
import android.text.InputType
import android.text.SpannableStringBuilder
import android.text.Spanned
import android.text.style.ForegroundColorSpan
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.*
import java.text.SimpleDateFormat
import java.util.*

class MainActivity : Activity() {

    companion object {
        private const val REQ_PICK_XLSX = 1001
    }

    private lateinit var repo: ConfigRepository
    private var storeDb = LinkedHashMap<String, StoreLinks>()
    private val stockRows = ArrayList<StockRow>()
    private val failedRowIndices = ArrayList<Int>()
    private var isDarkMode = false

    // Root & Cards
    private lateinit var rootLayout: LinearLayout
    private lateinit var mainScroll: ScrollView
    private val cardViews = ArrayList<LinearLayout>()
    private val primaryLabels = ArrayList<TextView>()
    private val mutedLabels = ArrayList<TextView>()
    private val inputEdits = ArrayList<EditText>()

    // Header & Stat Pills
    private lateinit var btnTheme: Button
    private lateinit var tvStatTotal: TextView
    private lateinit var tvStatSukses: TextView
    private lateinit var tvStatGagal: TextView
    private lateinit var tvStatSisa: TextView

    // Card 1: Data & Smart Table
    private lateinit var tvFileSource: TextView
    private lateinit var entKeep: EditText
    private lateinit var entSku: EditText
    private lateinit var entQty: EditText
    private lateinit var entGondola: EditText
    private lateinit var tableScroll: ScrollView
    private lateinit var tableRowsContainer: LinearLayout

    // Card 2: Konfigurasi Toko & PIC
    private lateinit var spinToko: Spinner
    private lateinit var spinJenis: Spinner
    private lateinit var entMulai: EditText
    private lateinit var entRetry: EditText
    private lateinit var entNamaPic: EditText
    private lateinit var spinPosisi: Spinner
    private lateinit var spinWorker: Spinner
    private lateinit var chkHeadless: CheckBox

    // Card 3: Status, Progress & Live WebView
    private lateinit var tvStatus: TextView
    private lateinit var tvEta: TextView
    private lateinit var progressBar: ProgressBar
    private lateinit var webViewWrapper: ScrollView
    private lateinit var webViewContainer: LinearLayout

    // Tombol Kontrol Utama
    private lateinit var btnStart: Button
    private lateinit var btnPause: Button
    private lateinit var btnStop: Button
    private lateinit var btnRetryFailed: Button

    // Card 4: Live Log
    private lateinit var logScroll: ScrollView
    private lateinit var tvLog: TextView
    private val logBuilder = SpannableStringBuilder()

    // Coordinator Otomasi WebView
    private lateinit var coordinator: GoogleFormAutomationCoordinator

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        repo = ConfigRepository(this)
        storeDb = repo.loadStoreDatabase()
        val prefs = repo.loadUserPrefs()
        isDarkMode = prefs.darkMode

        buildModernDashboardUI()
        applySavedPreferences(prefs)
        applyTheme()
        refreshTableUI()

        coordinator = GoogleFormAutomationCoordinator(
            context = this,
            webViewContainer = webViewContainer,
            onLog = { msg, type -> runOnUiThread { appendLog(msg, type) } },
            onStatusChange = { txt, col ->
                runOnUiThread {
                    tvStatus.text = txt
                    tvStatus.setTextColor(col)
                }
            },
            onEtaChange = { txt -> runOnUiThread { tvEta.text = txt } },
            onProgressChange = { pos, max ->
                runOnUiThread {
                    progressBar.max = maxOf(1, max)
                    progressBar.progress = pos.coerceIn(0, maxOf(1, max))
                }
            },
            onRowStatusChange = { idx, st, scroll ->
                runOnUiThread {
                    if (idx in stockRows.indices) {
                        stockRows[idx].status = st
                        updateSingleRowView(idx, scroll)
                        updateHeaderStats()
                    }
                }
            },
            onFinished = { wasStopped, _, lastSuccRow, succSession, totalData, failedList, _ ->
                runOnUiThread {
                    handleSessionFinished(wasStopped, lastSuccRow, succSession, totalData, failedList)
                }
            }
        )

        appendLog(
            "Sistem v6.1 (Android Native WebView Edition) siap: Smart Table, Multi-Worker, Auto-Retry & Dark Mode.",
            "INFO"
        )
    }

    private fun dp(v: Int): Int {
        return (v * resources.displayMetrics.density + 0.5f).toInt()
    }

    private fun makeRoundedBg(fillColor: Int, strokeColor: Int, radiusDp: Int, strokeDp: Int = 1): GradientDrawable {
        return GradientDrawable().apply {
            shape = GradientDrawable.RECTANGLE
            cornerRadius = dp(radiusDp).toFloat()
            setColor(fillColor)
            if (strokeDp > 0) {
                setStroke(dp(strokeDp), strokeColor)
            }
        }
    }

    private fun createStyledButton(text: String, bgHex: Int, textSizeSp: Float = 11.5f): Button {
        return Button(this).apply {
            this.text = text
            isAllCaps = false
            setTextColor(Color.WHITE)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, textSizeSp)
            typeface = Typeface.create("sans-serif-medium", Typeface.BOLD)
            background = makeRoundedBg(bgHex, adjustColor(bgHex, -25), 7)
            setPadding(dp(10), dp(4), dp(10), dp(4))
            minHeight = dp(34)
            minimumHeight = dp(34)
        }
    }

    private fun adjustColor(color: Int, delta: Int): Int {
        val r = (Color.red(color) + delta).coerceIn(0, 255)
        val g = (Color.green(color) + delta).coerceIn(0, 255)
        val b = (Color.blue(color) + delta).coerceIn(0, 255)
        return Color.rgb(r, g, b)
    }

    private fun buildModernDashboardUI() {
        rootLayout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            layoutParams = ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            )
        }

        // ====================================================================
        // 1. HEADER BANNER MODERN + 4 STAT PILLS + DARK MODE TOGGLE
        // ====================================================================
        val headerBanner = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(Color.rgb(15, 23, 42))
            setPadding(dp(12), dp(10), dp(12), dp(10))
        }

        val headerTopRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }

        val titleCol = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
        }

        val tvAppTitle = TextView(this).apply {
            text = "BOT KEEP STOCK OTOMATIS  v6.1"
            setTextColor(Color.rgb(248, 250, 252))
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 14.5f)
            typeface = Typeface.DEFAULT_BOLD
        }
        val tvAppSub = TextView(this).apply {
            text = "Android Native WebView • Multi-Worker • Smart Table"
            setTextColor(Color.rgb(148, 163, 184))
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 10.5f)
        }
        titleCol.addView(tvAppTitle)
        titleCol.addView(tvAppSub)

        btnTheme = createStyledButton("🌙 Dark", Color.rgb(30, 41, 59), 11f).apply {
            setOnClickListener {
                isDarkMode = !isDarkMode
                saveCurrentPrefs()
                applyTheme()
                refreshTableUI()
            }
        }

        headerTopRow.addView(titleCol)
        headerTopRow.addView(btnTheme)
        headerBanner.addView(headerTopRow)

        // Baris Stat Counter Pills (TOTAL, SUKSES, GAGAL, SISA)
        val statRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            layoutParams = LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
            ).apply { topMargin = dp(8) }
        }

        tvStatTotal = addStatPill(statRow, "TOTAL", Color.rgb(56, 189, 248))
        tvStatSukses = addStatPill(statRow, "SUKSES", Color.rgb(74, 222, 128))
        tvStatGagal = addStatPill(statRow, "GAGAL", Color.rgb(248, 113, 113))
        tvStatSisa = addStatPill(statRow, "SISA", Color.rgb(251, 191, 36))

        headerBanner.addView(statRow)
        rootLayout.addView(headerBanner)

        // Garis aksen biru di bawah header
        val accentBar = View(this).apply {
            layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(2))
            setBackgroundColor(Color.rgb(37, 99, 235))
        }
        rootLayout.addView(accentBar)

        // ====================================================================
        // SCROLLABLE BODY
        // ====================================================================
        mainScroll = ScrollView(this).apply {
            layoutParams = LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                0,
                1f
            )
            isFillViewport = true
        }

        val bodyContainer = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(10), dp(10), dp(10), dp(16))
        }

        // ====================================================================
        // KARTU 1: SUMBER DATA & TABEL INTERAKTIF (LIVE STATUS)
        // ====================================================================
        val card1 = createCard("1. Sumber Data & Tabel Interaktif (Live Status)")

        // Toolbar Tombol Sumber Data
        val toolbarRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            layoutParams = LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
            ).apply { topMargin = dp(6) }
        }

        val btnOpenXlsx = createStyledButton("📂 Buka .xlsx", Color.rgb(37, 99, 235)).apply {
            layoutParams = LinearLayout.LayoutParams(0, dp(34), 1.1f).apply { rightMargin = dp(5) }
            setOnClickListener { openExcelFilePicker() }
        }
        val btnPaste = createStyledButton("📋 Paste Excel", Color.rgb(2, 132, 199)).apply {
            layoutParams = LinearLayout.LayoutParams(0, dp(34), 1.1f).apply { rightMargin = dp(5) }
            setOnClickListener { pasteFromClipboard() }
        }
        val btnValidate = createStyledButton("✓ Validasi", Color.rgb(13, 148, 136)).apply {
            layoutParams = LinearLayout.LayoutParams(0, dp(34), 0.95f).apply { rightMargin = dp(5) }
            setOnClickListener { validateAndCheckDuplicates() }
        }
        val btnReset = createStyledButton("↺ Reset", Color.rgb(100, 116, 139)).apply {
            layoutParams = LinearLayout.LayoutParams(0, dp(34), 0.75f)
            setOnClickListener { confirmResetTable() }
        }

        toolbarRow.addView(btnOpenXlsx)
        toolbarRow.addView(btnPaste)
        toolbarRow.addView(btnValidate)
        toolbarRow.addView(btnReset)
        card1.addView(toolbarRow)

        tvFileSource = TextView(this).apply {
            text = "Sumber: Belum ada file dipilih (Buka .xlsx atau Paste dari Excel/Sheets HP)"
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 10.5f)
            setPadding(dp(2), dp(4), dp(2), dp(4))
        }
        mutedLabels.add(tvFileSource)
        card1.addView(tvFileSource)

        // Baris Tambah Manual Cepat
        val quickAddRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            layoutParams = LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
            ).apply { bottomMargin = dp(6) }
        }

        entKeep = createCompactEdit("Keep Stock").apply {
            layoutParams = LinearLayout.LayoutParams(0, dp(34), 1f).apply { rightMargin = dp(4) }
        }
        entSku = createCompactEdit("SKU").apply {
            layoutParams = LinearLayout.LayoutParams(0, dp(34), 1f).apply { rightMargin = dp(4) }
        }
        entQty = createCompactEdit("QTY", InputType.TYPE_CLASS_NUMBER or InputType.TYPE_NUMBER_FLAG_DECIMAL).apply {
            layoutParams = LinearLayout.LayoutParams(0, dp(34), 0.65f).apply { rightMargin = dp(4) }
        }
        entGondola = createCompactEdit("Gondola").apply {
            layoutParams = LinearLayout.LayoutParams(0, dp(34), 1f).apply { rightMargin = dp(4) }
        }
        val btnAddManual = createStyledButton("+ Tambah", Color.rgb(22, 163, 74)).apply {
            layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(34))
            setOnClickListener { addManualRow() }
        }

        quickAddRow.addView(entKeep)
        quickAddRow.addView(entSku)
        quickAddRow.addView(entQty)
        quickAddRow.addView(entGondola)
        quickAddRow.addView(btnAddManual)
        card1.addView(quickAddRow)

        // Header Kolom Smart Table
        val tableHeader = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setBackgroundColor(Color.rgb(30, 41, 59))
            setPadding(dp(6), dp(6), dp(6), dp(6))
        }
        addTableHeaderCell(tableHeader, "#", 0.45f)
        addTableHeaderCell(tableHeader, "Keep Stock", 1.15f)
        addTableHeaderCell(tableHeader, "SKU", 1.25f)
        addTableHeaderCell(tableHeader, "QTY", 0.65f)
        addTableHeaderCell(tableHeader, "Gondola", 1.1f)
        addTableHeaderCell(tableHeader, "Status", 1.4f)
        card1.addView(tableHeader)

        // Daftar Baris Smart Table (Scrollable)
        tableScroll = ScrollView(this).apply {
            layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(155))
            isNestedScrollingEnabled = true
        }
        tableRowsContainer = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
        }
        tableScroll.addView(tableRowsContainer)
        card1.addView(tableScroll)

        val tvTips = TextView(this).apply {
            text = "💡 Ketuk baris mana saja pada tabel untuk Mengedit atau Menghapus baris tersebut."
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 10f)
            setPadding(dp(2), dp(4), dp(2), 0)
        }
        mutedLabels.add(tvTips)
        card1.addView(tvTips)
        bodyContainer.addView(card1)

        // ====================================================================
        // KARTU 2: KONFIGURASI TOKO, IDENTITAS PIC & KECEPATAN
        // ====================================================================
        val card2 = createCard("2. Konfigurasi Toko, Identitas PIC & Kecepatan")

        // Baris 1: Cabang Toko + Tombol Kelola Toko & Jenis Form
        val cfgRow1 = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            layoutParams = LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
            ).apply { topMargin = dp(6) }
        }

        spinToko = Spinner(this).apply {
            layoutParams = LinearLayout.LayoutParams(0, dp(36), 1f).apply { rightMargin = dp(6) }
        }
        val btnKelolaToko = createStyledButton("⚙ Kelola Toko", Color.rgb(71, 85, 105)).apply {
            layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(34)).apply {
                rightMargin = dp(8)
            }
            setOnClickListener { showKelolaTokoDialog() }
        }
        spinJenis = Spinner(this).apply {
            layoutParams = LinearLayout.LayoutParams(0, dp(36), 0.75f)
            adapter = ArrayAdapter(
                this@MainActivity,
                android.R.layout.simple_spinner_dropdown_item,
                ConfigRepository.JENIS_LIST
            )
        }

        cfgRow1.addView(spinToko)
        cfgRow1.addView(btnKelolaToko)
        cfgRow1.addView(spinJenis)
        card2.addView(cfgRow1)

        // Baris 2: Nama PIC & Posisi
        val cfgRow2 = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            layoutParams = LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
            ).apply { topMargin = dp(6) }
        }

        entNamaPic = createCompactEdit("Nama Lengkap PIC").apply {
            layoutParams = LinearLayout.LayoutParams(0, dp(36), 1.3f).apply { rightMargin = dp(8) }
        }
        spinPosisi = Spinner(this).apply {
            layoutParams = LinearLayout.LayoutParams(0, dp(36), 0.9f)
            adapter = ArrayAdapter(
                this@MainActivity,
                android.R.layout.simple_spinner_dropdown_item,
                ConfigRepository.POSISI_LIST
            )
        }

        cfgRow2.addView(entNamaPic)
        cfgRow2.addView(spinPosisi)
        card2.addView(cfgRow2)

        // Baris 3: Mulai Data Ke, Maks Retry, Kecepatan Worker, & Headless
        val cfgRow3 = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            layoutParams = LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
            ).apply { topMargin = dp(6) }
        }

        val lblMulai = createSmallLabel("Mulai #:")
        entMulai = createCompactEdit("1", InputType.TYPE_CLASS_NUMBER).apply {
            setText("1")
            gravity = Gravity.CENTER
            layoutParams = LinearLayout.LayoutParams(dp(44), dp(34)).apply { rightMargin = dp(8) }
        }

        val lblRetry = createSmallLabel("Retry:")
        entRetry = createCompactEdit("3", InputType.TYPE_CLASS_NUMBER).apply {
            setText("3")
            gravity = Gravity.CENTER
            layoutParams = LinearLayout.LayoutParams(dp(40), dp(34)).apply { rightMargin = dp(8) }
        }

        spinWorker = Spinner(this).apply {
            layoutParams = LinearLayout.LayoutParams(0, dp(36), 1f).apply { rightMargin = dp(6) }
            adapter = ArrayAdapter(
                this@MainActivity,
                android.R.layout.simple_spinner_dropdown_item,
                ConfigRepository.WORKER_LIST
            )
        }

        chkHeadless = CheckBox(this).apply {
            text = "Headless"
            isChecked = true
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
            setOnCheckedChangeListener { _, isChecked ->
                updateWebViewWrapperVisibility(isChecked)
            }
        }
        primaryLabels.add(chkHeadless)

        cfgRow3.addView(lblMulai)
        cfgRow3.addView(entMulai)
        cfgRow3.addView(lblRetry)
        cfgRow3.addView(entRetry)
        cfgRow3.addView(spinWorker)
        cfgRow3.addView(chkHeadless)
        card2.addView(cfgRow3)

        bodyContainer.addView(card2)

        // ====================================================================
        // KARTU 3: STATUS PERGERAKAN, PROGRESS BAR & LIVE WEBVIEW
        // ====================================================================
        val card3 = createCard("3. Status Pergerakan & Progress")

        tvStatus = TextView(this).apply {
            text = "Bot siap dijalankan."
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 11.5f)
            typeface = Typeface.create("sans-serif", Typeface.ITALIC)
            setPadding(0, dp(4), 0, dp(2))
        }
        tvEta = TextView(this).apply {
            text = "Estimasi sisa waktu: -"
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
            typeface = Typeface.DEFAULT_BOLD
            setPadding(0, 0, 0, dp(4))
        }
        mutedLabels.add(tvEta)

        progressBar = ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal).apply {
            layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(12))
            max = 100
            progress = 0
        }

        card3.addView(tvStatus)
        card3.addView(tvEta)
        card3.addView(progressBar)

        // Wadah WebView Live (ScrollView memastikan WebView tetap berukuran penuh 320dp di memori meskipun tinggi wadah luar 1dp saat Headless)
        webViewWrapper = ScrollView(this).apply {
            layoutParams = LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                dp(1)
            )
            alpha = 0.01f
            isVerticalScrollBarEnabled = false
        }
        val webViewInnerBox = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            layoutParams = ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
            )
        }
        val tvWebPreviewTitle = createSmallLabel("🌐 Tampilan Live Google Form (WebView):")
        webViewContainer = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            layoutParams = LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
            )
        }
        webViewInnerBox.addView(tvWebPreviewTitle)
        webViewInnerBox.addView(webViewContainer)
        webViewWrapper.addView(webViewInnerBox)
        card3.addView(webViewWrapper)

        bodyContainer.addView(card3)

        // ====================================================================
        // DERETAN TOMBOL KONTROL UTAMA
        // ====================================================================
        val ctrlRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            layoutParams = LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
            ).apply { bottomMargin = dp(8) }
        }

        btnStart = createStyledButton("▶ JALANKAN", Color.rgb(22, 163, 74), 12f).apply {
            layoutParams = LinearLayout.LayoutParams(0, dp(40), 1.15f).apply { rightMargin = dp(5) }
            setOnClickListener { startBotProcess(false) }
        }
        btnPause = createStyledButton("⏸ PAUSE", Color.rgb(217, 119, 6), 11.5f).apply {
            layoutParams = LinearLayout.LayoutParams(0, dp(40), 0.9f).apply { rightMargin = dp(5) }
            isEnabled = false
            setOnClickListener { togglePauseBot() }
        }
        btnStop = createStyledButton("⏹ STOP", Color.rgb(220, 38, 38), 11.5f).apply {
            layoutParams = LinearLayout.LayoutParams(0, dp(40), 0.8f).apply { rightMargin = dp(5) }
            isEnabled = false
            setOnClickListener { stopBotProcess() }
        }
        btnRetryFailed = createStyledButton("↻ ULANGI (0)", Color.rgb(124, 58, 237), 11f).apply {
            layoutParams = LinearLayout.LayoutParams(0, dp(40), 1.15f)
            isEnabled = false
            setOnClickListener { startBotProcess(true) }
        }

        ctrlRow.addView(btnStart)
        ctrlRow.addView(btnPause)
        ctrlRow.addView(btnStop)
        ctrlRow.addView(btnRetryFailed)
        bodyContainer.addView(ctrlRow)

        // ====================================================================
        // KARTU 4: LIVE LOG AKTIVITAS
        // ====================================================================
        val card4 = createCard("4. Live Log Aktivitas")

        val logHeaderRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.END
            layoutParams = LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
            )
        }
        val btnClearLog = createStyledButton("Bersihkan Log", Color.rgb(100, 116, 139), 10.5f).apply {
            setOnClickListener {
                logBuilder.clear()
                tvLog.text = ""
            }
        }
        logHeaderRow.addView(btnClearLog)
        card4.addView(logHeaderRow)

        logScroll = ScrollView(this).apply {
            layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(145)).apply {
                topMargin = dp(4)
            }
            setBackgroundColor(Color.rgb(9, 14, 26))
            setPadding(dp(8), dp(6), dp(8), dp(6))
            isNestedScrollingEnabled = true
        }
        tvLog = TextView(this).apply {
            setTextColor(Color.rgb(96, 165, 250))
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 10.5f)
            typeface = Typeface.MONOSPACE
        }
        logScroll.addView(tvLog)
        card4.addView(logScroll)

        bodyContainer.addView(card4)
        mainScroll.addView(bodyContainer)
        rootLayout.addView(mainScroll)

        setContentView(rootLayout)
    }

    private fun addStatPill(parent: LinearLayout, label: String, accentColor: Int): TextView {
        val pill = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER
            background = makeRoundedBg(Color.rgb(30, 41, 59), accentColor, 7, 1)
            setPadding(dp(6), dp(4), dp(6), dp(4))
            layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f).apply {
                rightMargin = if (label != "SISA") dp(6) else 0
            }
        }
        val tvLbl = TextView(this).apply {
            text = label
            setTextColor(Color.rgb(148, 163, 184))
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 9.5f)
            typeface = Typeface.DEFAULT_BOLD
            gravity = Gravity.CENTER
        }
        val tvVal = TextView(this).apply {
            text = "0"
            setTextColor(accentColor)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 13.5f)
            typeface = Typeface.DEFAULT_BOLD
            gravity = Gravity.CENTER
        }
        pill.addView(tvLbl)
        pill.addView(tvVal)
        parent.addView(pill)
        return tvVal
    }

    private fun createCard(title: String): LinearLayout {
        val card = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(10), dp(10), dp(10), dp(10))
            layoutParams = LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
            ).apply { bottomMargin = dp(8) }
        }
        val titleRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        val accent = View(this).apply {
            layoutParams = LinearLayout.LayoutParams(dp(4), dp(15)).apply { rightMargin = dp(7) }
            setBackgroundColor(Color.rgb(37, 99, 235))
        }
        val tvTitle = TextView(this).apply {
            text = title
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 12.5f)
            typeface = Typeface.DEFAULT_BOLD
        }
        primaryLabels.add(tvTitle)
        titleRow.addView(accent)
        titleRow.addView(tvTitle)
        card.addView(titleRow)
        cardViews.add(card)
        return card
    }

    private fun createCompactEdit(hintText: String, inputTypeFlags: Int = InputType.TYPE_CLASS_TEXT): EditText {
        return EditText(this).apply {
            hint = hintText
            inputType = inputTypeFlags
            setSingleLine(true)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 11.5f)
            setPadding(dp(8), dp(4), dp(8), dp(4))
            inputEdits.add(this)
        }
    }

    private fun createSmallLabel(txt: String): TextView {
        return TextView(this).apply {
            text = txt
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
            typeface = Typeface.DEFAULT_BOLD
            setPadding(0, 0, dp(4), 0)
            primaryLabels.add(this)
        }
    }

    private fun addTableHeaderCell(parent: LinearLayout, text: String, weight: Float) {
        val tv = TextView(this).apply {
            this.text = text
            setTextColor(Color.WHITE)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 10.5f)
            typeface = Typeface.DEFAULT_BOLD
            gravity = Gravity.CENTER
            layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, weight)
        }
        parent.addView(tv)
    }

    // ====================================================================
    // TEMA DARK MODE / LIGHT MODE DINAMIS
    // ====================================================================
    private fun applyTheme() {
        val winBg = if (isDarkMode) Color.rgb(15, 23, 42) else Color.rgb(241, 245, 249)
        val cardBg = if (isDarkMode) Color.rgb(30, 41, 59) else Color.WHITE
        val cardBorder = if (isDarkMode) Color.rgb(51, 65, 85) else Color.rgb(203, 213, 225)
        val textPri = if (isDarkMode) Color.rgb(241, 245, 249) else Color.rgb(15, 23, 42)
        val textMut = if (isDarkMode) Color.rgb(148, 163, 184) else Color.rgb(100, 116, 139)
        val inputBg = if (isDarkMode) Color.rgb(15, 23, 42) else Color.rgb(248, 250, 252)

        rootLayout.setBackgroundColor(winBg)
        btnTheme.text = if (isDarkMode) "☀ Light" else "🌙 Dark"

        for (c in cardViews) {
            c.background = makeRoundedBg(cardBg, cardBorder, 10, 1)
        }
        for (lbl in primaryLabels) {
            lbl.setTextColor(textPri)
        }
        for (lbl in mutedLabels) {
            lbl.setTextColor(textMut)
        }
        for (ed in inputEdits) {
            ed.setTextColor(textPri)
            ed.setHintTextColor(textMut)
            ed.background = makeRoundedBg(inputBg, cardBorder, 6, 1)
        }
        tableScroll.background = makeRoundedBg(inputBg, cardBorder, 6, 1)
    }

    // ====================================================================
    // TABEL INTERAKTIF & COUNTER STATISTIK
    // ====================================================================
    private fun updateHeaderStats() {
        val total = stockRows.size
        var sukses = 0
        var gagal = 0
        for (r in stockRows) {
            if (r.status.contains("Sukses")) sukses++
            else if (r.status.contains("Gagal")) gagal++
        }
        val sisa = maxOf(0, total - sukses - gagal)

        tvStatTotal.text = total.toString()
        tvStatSukses.text = sukses.toString()
        tvStatGagal.text = gagal.toString()
        tvStatSisa.text = sisa.toString()
    }

    private fun refreshTableUI() {
        tableRowsContainer.removeAllViews()
        for (i in stockRows.indices) {
            tableRowsContainer.addView(createRowView(i, stockRows[i]))
        }
        updateHeaderStats()
    }

    private fun updateSingleRowView(index: Int, autoScroll: Boolean) {
        if (index < 0 || index >= tableRowsContainer.childCount) {
            refreshTableUI()
            return
        }
        val newView = createRowView(index, stockRows[index])
        tableRowsContainer.removeViewAt(index)
        tableRowsContainer.addView(newView, index)
        if (autoScroll) {
            tableScroll.post {
                tableScroll.smoothScrollTo(0, newView.top)
            }
        }
    }

    private fun createRowView(index: Int, row: StockRow): View {
        val st = row.status
        val bgCol: Int
        val fgCol: Int

        when {
            st.contains("Sukses") -> {
                bgCol = if (isDarkMode) Color.rgb(6, 78, 59) else Color.rgb(220, 252, 231)
                fgCol = if (isDarkMode) Color.rgb(167, 243, 208) else Color.rgb(22, 101, 52)
            }
            st.contains("Memproses") -> {
                bgCol = if (isDarkMode) Color.rgb(30, 58, 138) else Color.rgb(219, 234, 254)
                fgCol = if (isDarkMode) Color.rgb(191, 219, 254) else Color.rgb(30, 64, 175)
            }
            st.contains("Retry") || st.contains("Duplikat") || st.contains("Kosong") || st.contains("Invalid") -> {
                bgCol = if (isDarkMode) Color.rgb(120, 53, 15) else Color.rgb(254, 243, 199)
                fgCol = if (isDarkMode) Color.rgb(253, 230, 138) else Color.rgb(146, 64, 14)
            }
            st.contains("Gagal") -> {
                bgCol = if (isDarkMode) Color.rgb(127, 29, 29) else Color.rgb(254, 226, 226)
                fgCol = if (isDarkMode) Color.rgb(254, 202, 202) else Color.rgb(153, 27, 27)
            }
            else -> {
                val even = (index % 2 == 0)
                bgCol = if (isDarkMode) {
                    if (even) Color.rgb(15, 23, 42) else Color.rgb(23, 33, 54)
                } else {
                    if (even) Color.WHITE else Color.rgb(248, 250, 252)
                }
                fgCol = if (isDarkMode) Color.rgb(241, 245, 249) else Color.rgb(15, 23, 42)
            }
        }

        val rowLayout = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setBackgroundColor(bgCol)
            setPadding(dp(6), dp(7), dp(6), dp(7))
            setOnClickListener {
                if (!coordinator.isRunning.get()) {
                    showEditOrDeleteRowDialog(index)
                }
            }
        }

        addRowCell(rowLayout, (index + 1).toString(), 0.45f, fgCol, false)
        addRowCell(rowLayout, row.keepStock, 1.15f, fgCol, false)
        addRowCell(rowLayout, row.sku, 1.25f, fgCol, true)
        addRowCell(rowLayout, row.skuQty, 0.65f, fgCol, false)
        addRowCell(rowLayout, row.gondola, 1.1f, fgCol, false)
        addRowCell(rowLayout, row.status, 1.4f, fgCol, true)

        return rowLayout
    }

    private fun addRowCell(parent: LinearLayout, text: String, weight: Float, color: Int, bold: Boolean) {
        val tv = TextView(this).apply {
            this.text = text
            setTextColor(color)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 10.5f)
            typeface = if (bold) Typeface.DEFAULT_BOLD else Typeface.DEFAULT
            gravity = Gravity.CENTER
            setSingleLine(true)
            layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, weight)
        }
        parent.addView(tv)
    }

    private fun showEditOrDeleteRowDialog(index: Int) {
        if (index !in stockRows.indices) return
        val r = stockRows[index]

        val box = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(18), dp(10), dp(18), dp(4))
        }
        val eKeep = EditText(this).apply { hint = "Alamat Keep Stock"; setText(r.keepStock) }
        val eSku = EditText(this).apply { hint = "SKU"; setText(r.sku) }
        val eQty = EditText(this).apply {
            hint = "SKU QTY"
            inputType = InputType.TYPE_CLASS_NUMBER or InputType.TYPE_NUMBER_FLAG_DECIMAL
            setText(r.skuQty)
        }
        val eGon = EditText(this).apply { hint = "Alamat Gondola"; setText(r.gondola) }

        box.addView(eKeep)
        box.addView(eSku)
        box.addView(eQty)
        box.addView(eGon)

        AlertDialog.Builder(this)
            .setTitle("Edit Baris #${index + 1}")
            .setView(box)
            .setPositiveButton("Simpan") { _, _ ->
                r.keepStock = eKeep.text.toString().trim()
                r.sku = eSku.text.toString().trim()
                r.skuQty = eQty.text.toString().trim()
                r.gondola = eGon.text.toString().trim()
                r.status = "Menunggu"
                refreshTableUI()
            }
            .setNeutralButton("Hapus Baris") { _, _ ->
                stockRows.removeAt(index)
                refreshTableUI()
                appendLog("Baris #${index + 1} dihapus dari tabel.", "WARN")
            }
            .setNegativeButton("Batal", null)
            .show()
    }

    // ====================================================================
    // AKSI SUMBER DATA (BUKA .XLSX, PASTE EXCEL, TAMBAH MANUAL, VALIDASI)
    // ====================================================================
    private fun openExcelFilePicker() {
        if (coordinator.isRunning.get()) return
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "*/*"
            putExtra(
                Intent.EXTRA_MIME_TYPES,
                arrayOf(
                    "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",
                    "application/vnd.ms-excel",
                    "application/octet-stream"
                )
            )
        }
        startActivityForResult(intent, REQ_PICK_XLSX)
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == REQ_PICK_XLSX && resultCode == RESULT_OK) {
            val uri = data?.data ?: return
            loadExcelFromUri(uri)
        }
    }

    private fun loadExcelFromUri(uri: Uri) {
        val fileName = queryFileName(uri) ?: "file.xlsx"
        try {
            contentResolver.openInputStream(uri)?.use { inp ->
                val res = XlsxParser.readXlsxStream(inp)
                if (res.error != null) {
                    Toast.makeText(this, res.error, Toast.LENGTH_LONG).show()
                    appendLog("Gagal membaca $fileName: ${res.error}", "ERROR")
                    return
                }
                stockRows.clear()
                stockRows.addAll(res.rows)
                failedRowIndices.clear()
                updateRetryButtonState()
                refreshTableUI()

                tvFileSource.text = "Sumber Aktif: $fileName (${stockRows.size} baris)"
                tvFileSource.setTextColor(Color.rgb(34, 197, 94))
                appendLog("Berhasil memuat ${stockRows.size} baris dari file Excel '$fileName'.", "SUKSES")
            }
        } catch (e: Exception) {
            Toast.makeText(this, "Gagal membuka file: ${e.message}", Toast.LENGTH_LONG).show()
            appendLog("Error membuka file Excel: ${e.message}", "ERROR")
        }
    }

    private fun queryFileName(uri: Uri): String? {
        var name: String? = null
        contentResolver.query(uri, null, null, null, null)?.use { cursor ->
            if (cursor.moveToFirst()) {
                val idx = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                if (idx >= 0) name = cursor.getString(idx)
            }
        }
        return name
    }

    private fun pasteFromClipboard() {
        if (coordinator.isRunning.get()) return
        val cm = getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        val clip = cm.primaryClip
        if (clip == null || clip.itemCount == 0) {
            Toast.makeText(this, "Clipboard kosong! Copy dulu sel dari Excel/Sheets.", Toast.LENGTH_SHORT).show()
            return
        }
        val rawText = clip.getItemAt(0).coerceToText(this).toString()
        val parsed = XlsxParser.parseClipboardText(rawText)
        if (parsed.isEmpty()) {
            Toast.makeText(this, "Format tidak dikenali (Pastikan copy 4 kolom dari Excel/Sheets).", Toast.LENGTH_LONG).show()
            return
        }
        stockRows.addAll(parsed)
        refreshTableUI()
        tvFileSource.text = "Sumber Aktif: Clipboard / Input Tabel (${stockRows.size} baris)"
        tvFileSource.setTextColor(Color.rgb(34, 197, 94))
        appendLog("Berhasil menempelkan ${parsed.size} baris dari Clipboard (Total: ${stockRows.size} baris).", "SUKSES")
    }

    private fun addManualRow() {
        if (coordinator.isRunning.get()) return
        val k = entKeep.text.toString().trim()
        val s = entSku.text.toString().trim()
        val q = entQty.text.toString().trim()
        val g = entGondola.text.toString().trim()

        if (k.isEmpty() && s.isEmpty() && q.isEmpty() && g.isEmpty()) {
            Toast.makeText(this, "Isi kolom Keep Stock, SKU, QTY, dan Gondola terlebih dahulu!", Toast.LENGTH_SHORT).show()
            return
        }
        stockRows.add(StockRow(k, s, q, g, "Menunggu"))
        entKeep.setText("")
        entSku.setText("")
        entQty.setText("")
        entGondola.setText("")
        entKeep.requestFocus()
        refreshTableUI()
        appendLog("Menambahkan 1 baris manual ke tabel (Total: ${stockRows.size} baris).", "INFO")
    }

    private fun confirmResetTable() {
        if (coordinator.isRunning.get() || stockRows.isEmpty()) return
        AlertDialog.Builder(this)
            .setTitle("Kosongkan Tabel")
            .setMessage("Hapus seluruh ${stockRows.size} baris dari tabel saat ini?")
            .setPositiveButton("Ya, Kosongkan") { _, _ ->
                stockRows.clear()
                failedRowIndices.clear()
                updateRetryButtonState()
                refreshTableUI()
                tvFileSource.text = "Sumber: Tabel dikosongkan"
                appendLog("Seluruh isi tabel telah dikosongkan.", "WARN")
            }
            .setNegativeButton("Batal", null)
            .show()
    }

    private fun validateAndCheckDuplicates() {
        if (coordinator.isRunning.get()) return
        if (stockRows.isEmpty()) {
            Toast.makeText(this, "Tabel masih kosong!", Toast.LENGTH_SHORT).show()
            return
        }

        val seen = HashMap<String, Int>()
        val dupIndices = ArrayList<Int>()
        var emptyCount = 0
        var invalidQtyCount = 0

        for (i in stockRows.indices) {
            val r = stockRows[i]
            val k = DataValidator.bersihkanAngka(r.keepStock)
            val s = DataValidator.bersihkanAngka(r.sku)
            val q = DataValidator.bersihkanAngka(r.skuQty)
            val g = DataValidator.bersihkanAngka(r.gondola)

            if (k.isEmpty() || s.isEmpty() || q.isEmpty() || g.isEmpty()) {
                emptyCount++
                r.status = "! Kolom Kosong"
            } else if (!DataValidator.isValidPositiveNumber(q)) {
                invalidQtyCount++
                r.status = "! QTY Invalid"
            } else {
                val key = DataValidator.makeDuplicateKey(r)
                val firstIdx = seen[key]
                if (firstIdx != null) {
                    dupIndices.add(i)
                    r.status = "! Duplikat (#${firstIdx + 1})"
                } else {
                    seen[key] = i
                    r.status = "Menunggu"
                }
            }
        }
        refreshTableUI()

        if (dupIndices.isEmpty() && emptyCount == 0 && invalidQtyCount == 0) {
            AlertDialog.Builder(this)
                .setTitle("✓ Validasi Lolos")
                .setMessage("Seluruh ${stockRows.size} baris data valid, lengkap, dan bebas duplikat!")
                .setPositiveButton("OK", null)
                .show()
            appendLog("Validasi sukses: ${stockRows.size} baris bersih tanpa duplikat.", "SUKSES")
            return
        }

        val msg = StringBuilder("Hasil Pemeriksaan Tabel:\n\n")
        if (dupIndices.isNotEmpty()) msg.append("• Baris Duplikat: ${dupIndices.size} baris\n")
        if (emptyCount > 0) msg.append("• Kolom Kosong: $emptyCount baris\n")
        if (invalidQtyCount > 0) msg.append("• SKU QTY Bukan Angka: $invalidQtyCount baris\n")

        val dlg = AlertDialog.Builder(this)
            .setTitle("Deteksi Duplikat & Validasi")
            .setMessage(msg.toString())
            .setNegativeButton("Tutup", null)

        if (dupIndices.isNotEmpty()) {
            dlg.setPositiveButton("Hapus ${dupIndices.size} Duplikat") { _, _ ->
                for (idx in dupIndices.sortedDescending()) {
                    stockRows.removeAt(idx)
                }
                refreshTableUI()
                appendLog("Berhasil menghapus ${dupIndices.size} baris duplikat secara otomatis.", "WARN")
            }
        }
        dlg.show()
    }

    // ====================================================================
    // DIALOG KELOLA DATABASE TOKO (config_toko.json)
    // ====================================================================
    private fun showKelolaTokoDialog() {
        if (coordinator.isRunning.get()) return

        val box = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16), dp(10), dp(16), dp(6))
        }

        val codes = storeDb.keys.toList()
        val spinExisting = Spinner(this).apply {
            adapter = ArrayAdapter(
                this@MainActivity,
                android.R.layout.simple_spinner_dropdown_item,
                listOf("-- Pilih Toko untuk Edit / Baru --") + codes
            )
        }

        val eKode = EditText(this).apply { hint = "Kode Toko (Contoh: XBTK, XPHD)" }
        val eIn = EditText(this).apply { hint = "Link Google Form IN (viewform)" }
        val eOut = EditText(this).apply { hint = "Link Google Form OUT (viewform)" }

        spinExisting.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(parent: AdapterView<*>?, view: View?, position: Int, id: Long) {
                if (position > 0) {
                    val code = codes[position - 1]
                    val links = storeDb[code]
                    eKode.setText(code)
                    eIn.setText(links?.linkIn ?: "")
                    eOut.setText(links?.linkOut ?: "")
                }
            }
            override fun onNothingSelected(parent: AdapterView<*>?) {}
        }

        box.addView(spinExisting)
        box.addView(eKode)
        box.addView(eIn)
        box.addView(eOut)

        AlertDialog.Builder(this)
            .setTitle("Kelola Database Toko")
            .setView(box)
            .setPositiveButton("Simpan / Update") { _, _ ->
                val k = eKode.text.toString().trim().uppercase()
                val lIn = eIn.text.toString().trim()
                val lOut = eOut.text.toString().trim()
                if (k.isEmpty() || lIn.isEmpty() || lOut.isEmpty()) {
                    Toast.makeText(this, "Kode Toko, Link IN, dan Link OUT wajib diisi!", Toast.LENGTH_LONG).show()
                    return@setPositiveButton
                }
                storeDb[k] = StoreLinks(lIn, lOut)
                repo.saveStoreDatabase(storeDb)
                refreshSpinnerToko(k)
                appendLog("Toko '$k' berhasil disimpan ke database.", "SUKSES")
            }
            .setNeutralButton("Hapus Toko") { _, _ ->
                val k = eKode.text.toString().trim().uppercase()
                if (storeDb.size <= 1) {
                    Toast.makeText(this, "Minimal harus ada 1 toko di database!", Toast.LENGTH_SHORT).show()
                    return@setNeutralButton
                }
                if (storeDb.remove(k) != null) {
                    repo.saveStoreDatabase(storeDb)
                    refreshSpinnerToko(null)
                    appendLog("Toko '$k' telah dihapus dari database.", "WARN")
                }
            }
            .setNegativeButton("Tutup", null)
            .show()
    }

    private fun refreshSpinnerToko(selectCode: String?) {
        val list = storeDb.keys.toList()
        spinToko.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, list)
        if (selectCode != null) {
            val idx = list.indexOf(selectCode)
            if (idx >= 0) spinToko.setSelection(idx)
        }
    }

    private fun applySavedPreferences(prefs: UserPrefs) {
        refreshSpinnerToko(prefs.toko)
        val jenisIdx = ConfigRepository.JENIS_LIST.indexOf(prefs.jenis)
        if (jenisIdx >= 0) spinJenis.setSelection(jenisIdx)

        entNamaPic.setText(prefs.nama)
        val posIdx = ConfigRepository.POSISI_LIST.indexOf(prefs.posisi)
        if (posIdx >= 0) spinPosisi.setSelection(posIdx)

        spinWorker.setSelection((prefs.jumlahWorker - 1).coerceIn(0, 3))
        entRetry.setText(prefs.maxRetry.toString())
        chkHeadless.isChecked = prefs.headless
        updateWebViewWrapperVisibility(prefs.headless)
    }

    private fun updateWebViewWrapperVisibility(headless: Boolean) {
        if (headless) {
            webViewWrapper.layoutParams = LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                dp(1)
            ).apply { topMargin = 0 }
            webViewWrapper.alpha = 0.01f
        } else {
            webViewWrapper.layoutParams = LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
            ).apply { topMargin = dp(8) }
            webViewWrapper.alpha = 1.0f
        }
    }

    private fun saveCurrentPrefs() {
        val up = UserPrefs(
            toko = spinToko.selectedItem?.toString() ?: "XBTK",
            jenis = spinJenis.selectedItem?.toString() ?: "IN",
            nama = entNamaPic.text.toString().trim(),
            posisi = spinPosisi.selectedItem?.toString() ?: "RM",
            headless = chkHeadless.isChecked,
            jumlahWorker = (spinWorker.selectedItemPosition + 1).coerceIn(1, 4),
            maxRetry = (entRetry.text.toString().toIntOrNull() ?: 3).coerceIn(1, 10),
            darkMode = isDarkMode
        )
        repo.saveUserPrefs(up)
    }

    // ====================================================================
    // EKSEKUSI BOT OTOMASI GOOGLE FORM
    // ====================================================================
    private fun startBotProcess(onlyRetryFailed: Boolean) {
        if (coordinator.isRunning.get()) return
        if (stockRows.isEmpty()) {
            Toast.makeText(this, "Tabel data masih kosong! Buka file .xlsx atau Paste data terlebih dahulu.", Toast.LENGTH_LONG).show()
            return
        }

        val toko = spinToko.selectedItem?.toString() ?: ""
        val jenis = spinJenis.selectedItem?.toString() ?: "IN"
        val nama = entNamaPic.text.toString().trim()
        val posisi = spinPosisi.selectedItem?.toString() ?: "RM"

        if (toko.isEmpty() || !storeDb.containsKey(toko) || nama.isEmpty()) {
            Toast.makeText(this, "Pilih Toko dan isi Nama Lengkap PIC terlebih dahulu!", Toast.LENGTH_LONG).show()
            return
        }

        val numWorkers = (spinWorker.selectedItemPosition + 1).coerceIn(1, 4)
        val maxRetry = (entRetry.text.toString().toIntOrNull() ?: 3).coerceIn(1, 10)
        val headless = chkHeadless.isChecked

        val targetIndices = ArrayList<Int>()
        if (onlyRetryFailed) {
            for (idx in failedRowIndices) {
                if (idx in stockRows.indices) targetIndices.add(idx)
            }
            if (targetIndices.isEmpty()) {
                Toast.makeText(this, "Tidak ada baris gagal untuk diulangi.", Toast.LENGTH_SHORT).show()
                return
            }
        } else {
            val startRow = (entMulai.text.toString().toIntOrNull() ?: 1).coerceIn(1, stockRows.size)
            for (idx in (startRow - 1) until stockRows.size) {
                targetIndices.add(idx)
            }
        }

        saveCurrentPrefs()
        failedRowIndices.clear()
        updateRetryButtonState()
        setRunningControlState(true)

        val links = storeDb[toko]!!
        val targetUrl = if (jenis == "IN") links.linkIn else links.linkOut

        coordinator.startAutomation(
            rows = stockRows,
            targetIndices = targetIndices,
            linkForm = targetUrl,
            namaIdentitas = nama,
            posisiIdentitas = posisi,
            headlessMode = headless,
            numWorkers = numWorkers,
            maxRetry = maxRetry
        )
    }

    private fun togglePauseBot() {
        if (!coordinator.isRunning.get()) return
        if (!coordinator.isPaused.get()) {
            coordinator.isPaused.set(true)
            btnPause.text = "▶ LANJUT"
            appendLog("Proses dijeda sementara oleh pengguna.", "WARN")
        } else {
            coordinator.isPaused.set(false)
            btnPause.text = "⏸ PAUSE"
            appendLog("Proses dilanjutkan kembali...", "INFO")
        }
    }

    private fun stopBotProcess() {
        if (!coordinator.isRunning.get()) return
        AlertDialog.Builder(this)
            .setTitle("Hentikan Proses?")
            .setMessage("Yakin ingin menghentikan proses pengisian otomatis sekarang?")
            .setPositiveButton("Ya, Stop") { _, _ ->
                coordinator.stopAll()
                tvStatus.text = "Menghentikan semua WebView worker..."
                tvStatus.setTextColor(Color.rgb(239, 68, 68))
                appendLog("Perintah STOP dikirim. Menghentikan seluruh worker...", "ERROR")
            }
            .setNegativeButton("Tidak", null)
            .show()
    }

    private fun handleSessionFinished(
        wasStopped: Boolean,
        lastSuccRow: Int,
        succSession: Int,
        totalData: Int,
        failedList: List<Int>
    ) {
        failedRowIndices.clear()
        failedRowIndices.addAll(failedList)
        setRunningControlState(false)
        updateHeaderStats()

        if (wasStopped) {
            tvStatus.text = "Proses dihentikan oleh pengguna."
            tvStatus.setTextColor(Color.rgb(239, 68, 68))
            tvEta.text = "Estimasi sisa waktu: Dihentikan"
            if (lastSuccRow in 1 until totalData) {
                entMulai.setText((lastSuccRow + 1).toString())
            }
            appendLog("Proses dihentikan. Berhasil sesi ini: $succSession baris.", "WARN")
        } else if (failedList.isNotEmpty()) {
            tvStatus.text = "Selesai ($succSession Sukses, ${failedList.size} Gagal)."
            tvStatus.setTextColor(Color.rgb(245, 158, 11))
            tvEta.text = "Ketuk '↻ ULANGI (${failedList.size})' untuk memproses ulang baris gagal"
            appendLog(
                "Selesai: $succSession sukses, ${failedList.size} baris gagal. Ketuk tombol '↻ ULANGI' untuk mengulangi baris gagal.",
                "WARN"
            )
            AlertDialog.Builder(this)
                .setTitle("Selesai (Ada Baris Gagal)")
                .setMessage(
                    "• Berhasil diinput: $succSession baris\n" +
                    "• Gagal setelah batas retry: ${failedList.size} baris\n\n" +
                    "Ketuk tombol ungu '↻ ULANGI (${failedList.size})' untuk mengulangi khusus baris yang gagal saja."
                )
                .setPositiveButton("OK", null)
                .show()
        } else {
            tvStatus.text = "Selesai! Semua data berhasil diinput."
            tvStatus.setTextColor(Color.rgb(34, 197, 94))
            tvEta.text = "Estimasi sisa waktu: 0 detik (100% Selesai)"
            entMulai.setText("1")
            appendLog("SELURUH DATA ($succSession baris) BERHASIL DIINPUT TANPA TERLEWAT!", "SUKSES")
            AlertDialog.Builder(this)
                .setTitle("✓ Selesai 100%")
                .setMessage("Seluruh $succSession baris data telah berhasil dikirim ke Google Form!")
                .setPositiveButton("Mantap", null)
                .show()
        }
    }

    private fun setRunningControlState(running: Boolean) {
        btnStart.isEnabled = !running
        btnPause.isEnabled = running
        btnStop.isEnabled = running
        if (!running) {
            btnPause.text = "⏸ PAUSE"
        }
        updateRetryButtonState()
    }

    private fun updateRetryButtonState() {
        val cnt = failedRowIndices.size
        btnRetryFailed.text = "↻ ULANGI ($cnt)"
        btnRetryFailed.isEnabled = !coordinator.isRunning.get() && cnt > 0
    }

    private fun appendLog(message: String, type: String) {
        val timeStr = SimpleDateFormat("HH:mm:ss", Locale.getDefault()).format(Date())
        val line = "[$timeStr] $message\n"
        val color = when (type) {
            "SUKSES" -> Color.rgb(74, 222, 128)
            "ERROR" -> Color.rgb(248, 113, 113)
            "WARN" -> Color.rgb(251, 191, 36)
            else -> Color.rgb(96, 165, 250)
        }
        val start = logBuilder.length
        logBuilder.append(line)
        logBuilder.setSpan(
            ForegroundColorSpan(color),
            start,
            logBuilder.length,
            Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
        )
        tvLog.text = logBuilder
        logScroll.post {
            logScroll.fullScroll(View.FOCUS_DOWN)
        }
    }

    override fun onDestroy() {
        coordinator.stopAll()
        coordinator.cleanupWebViews()
        super.onDestroy()
    }
}
