# Bot Keep Stock Otomatis v6.1 — Android Native Edition (`.apk`)

Versi **Android Native (Kotlin + Multi-WebView Engine)** dari **Bot Keep Stock Otomatis v6.1** yang berjalan mandiri langsung di HP Android tanpa memerlukan PC maupun `chromedriver`.

---

## ✨ Fitur Utama (Setara dengan Versi C++ v6.1 Modern UI)

1. **Modern Dashboard v6.1 & Dark / Light Mode (`🌙 Dark` / `☀ Light`)**
   - Header Banner modern dilengkapi **4 Stat Counter Pills Real-Time**: **`TOTAL`**, **`SUKSES`**, **`GAGAL`**, dan **`SISA`**.
   - Tema **Dark Mode / Light Mode** instan yang otomatis tersimpan di memori HP.
2. **Unified Smart Table & Pembaca `.xlsx` Bawaan (Zero-Dependency)**
   - **📂 Buka `.xlsx`**: Membaca file Excel `.xlsx` langsung dari penyimpanan HP / WhatsApp / Telegram tanpa library berat (ukuran APK sangat kecil **~1.5 MB**).
   - **📋 Paste Excel**: Tempel (*Copy-Paste*) langsung dari aplikasi Microsoft Excel Android atau Google Sheets Android.
   - **✓ Validasi & Deteksi Duplikat**: Cek baris ganda, kolom kosong, atau `SKU QTY` bukan angka + hapus seluruh baris duplikat dalam 1 ketukan.
   - **Edit / Hapus Baris Cepat**: Ketuk baris mana saja di tabel untuk mengedit nilai sel atau menghapus baris.
3. **Engine Otomasi Multi-WebView Paralel (`1–4 Browser Worker`)**
   - Mengisi **Layer 1 (Nama PIC & Posisi)** dan **Layer 2 (Alamat Keep Stock, SKU, SKU QTY, Alamat Gondola)** di Google Form secara otomatis menggunakan injeksi XPath/DOM JavaScript dengan `User-Agent` Desktop Chrome.
   - Mendukung **1 hingga 4 WebView Paralel** yang berjalan serentak.
   - Mode **Headless (Background)** atau **Tampilkan Live Google Form (WebView)** langsung di dalam layar aplikasi.
   - Dilengkapi **Maks Retry (1–10x)** dan tombol ungu **`↻ ULANGI (N)`** khusus untuk mengulangi baris yang gagal saja.

---

## 🚀 Cara Mendapatkan File `.apk` (Tanpa Install Android Studio di PC)

Project ini sudah dilengkapi workflow **GitHub Actions** ([`.github/workflows/build-apk.yml`](.github/workflows/build-apk.yml)) yang akan mengompilasi file `.apk` secara otomatis di server GitHub:

1. Buat repository baru di GitHub (boleh **Private** maupun **Public**).
2. Upload / *push* isi folder project ini ke GitHub:
   ```bash
   git init
   git add .
   git commit -m "Bot Keep Stock v6.1 Desktop & Android"
   git branch -M main
   git remote add origin https://github.com/USERNAME/NAMA_REPO.git
   git push -u origin main
   ```
3. Buka tab **Actions** di repository GitHub Anda -> klik workflow **Build BotKeepStock Android APK**.
4. Tunggu ±2 menit hingga centang hijau, lalu unduh **`BotKeepStock-v6.1-Android-APK`** (`BotKeepStock-v6.1-Release.apk` / `BotKeepStock-v6.1-Debug.apk`) dan instal di HP Android Anda!
