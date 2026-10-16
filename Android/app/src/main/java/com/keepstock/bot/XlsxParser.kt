package com.keepstock.bot

import android.util.Xml
import org.xmlpull.v1.XmlPullParser
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.InputStream
import java.util.zip.ZipInputStream

object XlsxParser {

    data class ParseResult(
        val rows: List<StockRow>,
        val error: String? = null
    )

    // Baca file .xlsx langsung dari InputStream (Zero-Dependency menggunakan ZipInputStream + XmlPullParser bawaan Android)
    fun readXlsxStream(inputStream: InputStream): ParseResult {
        val entries = HashMap<String, ByteArray>()
        try {
            ZipInputStream(inputStream).use { zis ->
                var entry = zis.nextEntry
                while (entry != null) {
                    if (!entry.isDirectory) {
                        val name = entry.name
                        if (name == "xl/sharedStrings.xml" || name.startsWith("xl/worksheets/sheet")) {
                            val baos = ByteArrayOutputStream()
                            val buf = ByteArray(4096)
                            var len: Int
                            while (zis.read(buf).also { len = it } > 0) {
                                baos.write(buf, 0, len)
                            }
                            entries[name] = baos.toByteArray()
                        }
                    }
                    zis.closeEntry()
                    entry = zis.nextEntry
                }
            }
        } catch (e: Exception) {
            return ParseResult(emptyList(), "Gagal membuka struktur ZIP file Excel (.xlsx): ${e.message}")
        }

        val sheetBytes = entries["xl/worksheets/sheet1.xml"]
            ?: entries.entries.firstOrNull { it.key.startsWith("xl/worksheets/sheet") }?.value
            ?: return ParseResult(emptyList(), "Worksheet tidak ditemukan di dalam file .xlsx!")

        val sharedStrings = entries["xl/sharedStrings.xml"]?.let { parseSharedStrings(it) } ?: emptyList()
        val rawGrid = parseWorksheet(sheetBytes, sharedStrings)

        if (rawGrid.isEmpty()) {
            return ParseResult(emptyList(), "File Excel kosong!")
        }

        // Cari baris header (Alamat Keep Stock, SKU, SKU QTY, Alamat Gondola) atau fallback ke kolom 0,1,2,3
        var headerRowIdx = -1
        var colKeep = 0
        var colSku = 1
        var colQty = 2
        var colGondola = 3

        for (rIdx in 0 until minOf(10, rawGrid.size)) {
            val row = rawGrid[rIdx]
            var foundKeep = -1
            var foundSku = -1
            var foundQty = -1
            var foundGon = -1

            for (cIdx in row.indices) {
                val cellNorm = row[cIdx].trim().lowercase()
                when {
                    cellNorm == "alamat keep stock" || cellNorm == "keep stock" || cellNorm == "keepstock" -> foundKeep = cIdx
                    cellNorm == "sku" || cellNorm == "kode barang" -> foundSku = cIdx
                    cellNorm == "sku qty" || cellNorm == "qty" || cellNorm == "jumlah" -> foundQty = cIdx
                    cellNorm == "alamat gondola" || cellNorm == "gondola" -> foundGon = cIdx
                }
            }

            if (foundKeep >= 0 && foundSku >= 0 && foundQty >= 0 && foundGon >= 0) {
                headerRowIdx = rIdx
                colKeep = foundKeep
                colSku = foundSku
                colQty = foundQty
                colGondola = foundGon
                break
            }
        }

        val startRow = if (headerRowIdx >= 0) headerRowIdx + 1 else 0
        val resultRows = ArrayList<StockRow>()

        for (rIdx in startRow until rawGrid.size) {
            val r = rawGrid[rIdx]
            val vKeep = DataValidator.bersihkanAngka(r.getOrNull(colKeep))
            val vSku = DataValidator.bersihkanAngka(r.getOrNull(colSku))
            val vQty = DataValidator.bersihkanAngka(r.getOrNull(colQty))
            val vGon = DataValidator.bersihkanAngka(r.getOrNull(colGondola))

            if (vKeep.isEmpty() && vSku.isEmpty() && vQty.isEmpty() && vGon.isEmpty()) {
                continue
            }

            // Lewati jika baris pertama tanpa header kebetulan berisi teks judul
            if (headerRowIdx == -1 && rIdx == 0 &&
                (vKeep.lowercase().contains("keep") || vSku.lowercase() == "sku")
            ) {
                continue
            }

            resultRows.add(
                StockRow(
                    keepStock = vKeep,
                    sku = vSku,
                    skuQty = vQty,
                    gondola = vGon,
                    status = "Menunggu"
                )
            )
        }

        return ParseResult(resultRows, null)
    }

    private fun parseSharedStrings(xmlBytes: ByteArray): List<String> {
        val list = ArrayList<String>()
        try {
            val parser = Xml.newPullParser()
            parser.setInput(ByteArrayInputStream(xmlBytes), "UTF-8")
            var eventType = parser.eventType
            var inSi = false
            val sb = StringBuilder()

            while (eventType != XmlPullParser.END_DOCUMENT) {
                when (eventType) {
                    XmlPullParser.START_TAG -> {
                        if (parser.name == "si") {
                            inSi = true
                            sb.setLength(0)
                        } else if (inSi && parser.name == "t") {
                            sb.append(parser.nextText())
                        }
                    }
                    XmlPullParser.END_TAG -> {
                        if (parser.name == "si" && inSi) {
                            list.add(sb.toString())
                            inSi = false
                        }
                    }
                }
                eventType = parser.next()
            }
        } catch (_: Exception) {
        }
        return list
    }

    private fun parseWorksheet(xmlBytes: ByteArray, sharedStrings: List<String>): List<List<String>> {
        val rows = ArrayList<List<String>>()
        try {
            val parser = Xml.newPullParser()
            parser.setInput(ByteArrayInputStream(xmlBytes), "UTF-8")
            var eventType = parser.eventType

            var currentRow = ArrayList<String>()
            var cellType = ""
            var cellColIdx = -1
            var cellValue = ""

            while (eventType != XmlPullParser.END_DOCUMENT) {
                when (eventType) {
                    XmlPullParser.START_TAG -> {
                        when (parser.name) {
                            "row" -> {
                                currentRow = ArrayList()
                            }
                            "c" -> {
                                val ref = parser.getAttributeValue(null, "r") ?: ""
                                cellType = parser.getAttributeValue(null, "t") ?: ""
                                cellColIdx = parseColIndex(ref)
                                cellValue = ""
                            }
                            "v", "t" -> {
                                cellValue = parser.nextText()
                            }
                        }
                    }
                    XmlPullParser.END_TAG -> {
                        when (parser.name) {
                            "c" -> {
                                val resolved = if (cellType == "s") {
                                    val idx = cellValue.trim().toIntOrNull() ?: -1
                                    sharedStrings.getOrElse(idx) { "" }
                                } else {
                                    cellValue
                                }
                                val targetCol = if (cellColIdx >= 0) cellColIdx else currentRow.size
                                while (currentRow.size <= targetCol) {
                                    currentRow.add("")
                                }
                                currentRow[targetCol] = resolved
                            }
                            "row" -> {
                                if (currentRow.any { it.isNotBlank() }) {
                                    rows.add(currentRow)
                                }
                            }
                        }
                    }
                }
                eventType = parser.next()
            }
        } catch (_: Exception) {
        }
        return rows
    }

    private fun parseColIndex(cellRef: String): Int {
        var col = 0
        var hasLetters = false
        for (c in cellRef) {
            if (c in 'A'..'Z') {
                col = col * 26 + (c - 'A' + 1)
                hasLetters = true
            } else if (c in 'a'..'z') {
                col = col * 26 + (c - 'a' + 1)
                hasLetters = true
            } else {
                break
            }
        }
        return if (hasLetters) col - 1 else -1
    }

    // Parse hasil Copy-Paste dari Excel / Google Sheets / Clipboard HP
    fun parseClipboardText(rawClip: String): List<StockRow> {
        val result = ArrayList<StockRow>()
        val lines = rawClip.split("\r\n", "\n", "\r")
        for (line in lines) {
            if (line.isBlank()) continue
            val cols = if (line.contains('\t')) {
                line.split('\t')
            } else if (line.contains(';')) {
                line.split(';')
            } else {
                line.split(',')
            }
            if (cols.size < 2) continue

            val c0 = DataValidator.bersihkanAngka(cols.getOrNull(0))
            val c1 = DataValidator.bersihkanAngka(cols.getOrNull(1))
            val c2 = DataValidator.bersihkanAngka(cols.getOrNull(2))
            val c3 = DataValidator.bersihkanAngka(cols.getOrNull(3))

            if (c0.isEmpty() && c1.isEmpty() && c2.isEmpty() && c3.isEmpty()) continue

            // Abaikan baris header jika ikut ter-copy dari Excel
            val lower0 = c0.lowercase()
            val lower1 = c1.lowercase()
            if (lower0.contains("alamat keep") || lower0 == "keep stock" || lower1 == "sku") {
                continue
            }

            result.add(
                StockRow(
                    keepStock = c0,
                    sku = c1,
                    skuQty = c2,
                    gondola = c3,
                    status = "Menunggu"
                )
            )
        }
        return result
    }
}
