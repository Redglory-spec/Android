package com.keepstock.bot

import android.content.Context
import org.json.JSONObject

data class StockRow(
    var keepStock: String,
    var sku: String,
    var skuQty: String,
    var gondola: String,
    var status: String = "Menunggu"
)

data class StoreLinks(
    var linkIn: String,
    var linkOut: String
)

data class UserPrefs(
    var toko: String = "XBTK",
    var jenis: String = "IN",
    var nama: String = "",
    var posisi: String = "RM",
    var headless: Boolean = true,
    var jumlahWorker: Int = 1,
    var maxRetry: Int = 3,
    var darkMode: Boolean = false
)

object DataValidator {
    fun bersihkanAngka(raw: String?): String {
        if (raw == null) return ""
        var txt = raw.trim()
        if (txt.equals("nan", ignoreCase = true) || txt.equals("null", ignoreCase = true)) {
            return ""
        }
        if (txt.endsWith(".0")) {
            txt = txt.substring(0, txt.length - 2)
        }
        return txt.trim()
    }

    fun isValidPositiveNumber(raw: String?): Boolean {
        val s = bersihkanAngka(raw)
        if (s.isEmpty()) return false
        var dotCount = 0
        var digitCount = 0
        for (i in s.indices) {
            val c = s[i]
            if (c in '0'..'9') {
                digitCount++
            } else if (c == '.' || c == ',') {
                dotCount++
                if (dotCount > 1) return false
            } else {
                return false
            }
        }
        if (digitCount == 0) return false
        return try {
            val v = s.replace(',', '.').toDouble()
            v > 0.0
        } catch (_: Exception) {
            false
        }
    }

    fun makeDuplicateKey(r: StockRow): String {
        return "${bersihkanAngka(r.keepStock).uppercase()}|" +
               "${bersihkanAngka(r.sku).uppercase()}|" +
               "${bersihkanAngka(r.gondola).uppercase()}"
    }
}

class ConfigRepository(context: Context) {
    private val prefs = context.getSharedPreferences("bot_keepstock_v61_prefs", Context.MODE_PRIVATE)

    companion object {
        val POSISI_LIST = listOf("RM", "AM", "DM", "BM", "ABM", "MP", "AMP")
        val JENIS_LIST = listOf("IN", "OUT")
        val WORKER_LIST = listOf(
            "1 Browser (Normal)",
            "2 Browser (Cepat 2x)",
            "3 Browser (Turbo 3x)",
            "4 Browser (Ekstrem 4x)"
        )

        fun getDefaultStores(): LinkedHashMap<String, StoreLinks> {
            val map = LinkedHashMap<String, StoreLinks>()
            map["XBTK"] = StoreLinks(
                "https://docs.google.com/forms/d/e/1FAIpQLSetvyJul0k_KWbrm5BHvKqsquJrZtdwqoKwB7WJTIt2zGqH3w/viewform",
                "https://docs.google.com/forms/d/e/1FAIpQLSdmQ-ltvneM9YSKpcS6lgK-a_xPjNEJsNwGSR9WpMseYeLYow/viewform"
            )
            map["XPHD"] = StoreLinks(
                "https://docs.google.com/forms/d/e/1FAIpQLSeR0A3LBGOA2Q50Vh621OqT6xFDm-zyabnQWzjVx_XhU0Sq5Q/viewform",
                "https://docs.google.com/forms/d/e/1FAIpQLSeTe9lLsPjyRk20M2hVYiPFlnTSQqgE780-7_iGksWWE9SVXA/viewform"
            )
            map["XRTO"] = StoreLinks(
                "https://docs.google.com/forms/d/e/1FAIpQLSe7WWrZvRv8QT12wqLnjFUnoeyhEnwfJVAIPBAOiJpu-uxNQA/viewform",
                "https://docs.google.com/forms/d/e/1FAIpQLSewduPWNSS-jYYqV_z_5FbbWHP-T18QIICAncpfTD6CqT9EvA/viewform"
            )
            return map
        }
    }

    fun loadStoreDatabase(): LinkedHashMap<String, StoreLinks> {
        val raw = prefs.getString("config_toko_json", null)
        if (raw.isNullOrBlank()) {
            val def = getDefaultStores()
            saveStoreDatabase(def)
            return def
        }
        return try {
            val obj = JSONObject(raw)
            val result = LinkedHashMap<String, StoreLinks>()
            val keys = obj.keys().asSequence().toList().sorted()
            for (k in keys) {
                val item = obj.optJSONObject(k) ?: continue
                val linkIn = item.optString("IN", "")
                val linkOut = item.optString("OUT", "")
                if (linkIn.isNotBlank() && linkOut.isNotBlank()) {
                    result[k] = StoreLinks(linkIn, linkOut)
                }
            }
            if (result.isEmpty()) getDefaultStores() else result
        } catch (_: Exception) {
            getDefaultStores()
        }
    }

    fun saveStoreDatabase(db: Map<String, StoreLinks>) {
        val root = JSONObject()
        for ((code, links) in db) {
            val item = JSONObject()
            item.put("IN", links.linkIn)
            item.put("OUT", links.linkOut)
            root.put(code, item)
        }
        prefs.edit().putString("config_toko_json", root.toString()).apply()
    }

    fun loadUserPrefs(): UserPrefs {
        return UserPrefs(
            toko = prefs.getString("toko", "XBTK") ?: "XBTK",
            jenis = prefs.getString("jenis", "IN") ?: "IN",
            nama = prefs.getString("nama", "") ?: "",
            posisi = prefs.getString("posisi", "RM") ?: "RM",
            headless = prefs.getBoolean("headless", true),
            jumlahWorker = prefs.getInt("jumlah_worker", 1).coerceIn(1, 4),
            maxRetry = prefs.getInt("max_retry", 3).coerceIn(1, 10),
            darkMode = prefs.getBoolean("dark_mode", false)
        )
    }

    fun saveUserPrefs(up: UserPrefs) {
        prefs.edit()
            .putString("toko", up.toko)
            .putString("jenis", up.jenis)
            .putString("nama", up.nama)
            .putString("posisi", up.posisi)
            .putBoolean("headless", up.headless)
            .putInt("jumlah_worker", up.jumlahWorker.coerceIn(1, 4))
            .putInt("max_retry", up.maxRetry.coerceIn(1, 10))
            .putBoolean("dark_mode", up.darkMode)
            .apply()
    }
}
