import tkinter as tk
from tkinter import ttk, messagebox, filedialog
from tkinter.scrolledtext import ScrolledText
import threading
import pandas as pd
import time
import json
import os
from datetime import datetime

# ===================================================================
# PAKSA IMPORT SELURUH SUB-MODUL SELENIUM AGAR TERBUNGKUS KE EXE
# ===================================================================
from selenium import webdriver
from selenium.webdriver.common.by import By
from selenium.webdriver.chrome.options import Options
from selenium.webdriver.support.ui import WebDriverWait
from selenium.webdriver.support import expected_conditions as EC
import selenium.webdriver.chrome.webdriver
import selenium.webdriver.chrome.service

filepath_excel = ""
FILE_KONFIG_TOKO = "config_toko.json"
FILE_PREFERENSI = "user_prefs.json"
KOLOM_WAJIB = ['Alamat Keep Stock', 'SKU', 'SKU QTY', 'Alamat Gondola']

# Variabel kontrol untuk Pause dan Stop
is_paused = False
is_stopped = False

# ===================================================================
# DATABASE DEFAULT (AKAN DISIMPAN OTOMATIS KE config_toko.json)
# ===================================================================
DEFAULT_DATABASE_FORM = {
    "XBTK": {
        "IN": "https://docs.google.com/forms/d/e/1FAIpQLSetvyJul0k_KWbrm5BHvKqsquJrZtdwqoKwB7WJTIt2zGqH3w/viewform",
        "OUT": "https://docs.google.com/forms/d/e/1FAIpQLSdmQ-ltvneM9YSKpcS6lgK-a_xPjNEJsNwGSR9WpMseYeLYow/viewform"
    },
    "XPHD": {
        "IN": "https://docs.google.com/forms/d/e/1FAIpQLSeR0A3LBGOA2Q50Vh621OqT6xFDm-zyabnQWzjVx_XhU0Sq5Q/viewform",
        "OUT": "https://docs.google.com/forms/d/e/1FAIpQLSeTe9lLsPjyRk20M2hVYiPFlnTSQqgE780-7_iGksWWE9SVXA/viewform"
    },
    "XRTO": {
        "IN": "https://docs.google.com/forms/d/e/1FAIpQLSe7WWrZvRv8QT12wqLnjFUnoeyhEnwfJVAIPBAOiJpu-uxNQA/viewform",
        "OUT": "https://docs.google.com/forms/d/e/1FAIpQLSewduPWNSS-jYYqV_z_5FbbWHP-T18QIICAncpfTD6CqT9EvA/viewform"
    }
}

# ==================== MANAJEMEN KONFIGURASI & PREFERENSI ====================
def muat_database_toko():
    if not os.path.exists(FILE_KONFIG_TOKO):
        simpan_database_toko(DEFAULT_DATABASE_FORM)
        return DEFAULT_DATABASE_FORM.copy()
    try:
        with open(FILE_KONFIG_TOKO, "r", encoding="utf-8") as f:
            return json.load(f)
    except Exception as e:
        messagebox.showwarning("Peringatan", f"Gagal membaca {FILE_KONFIG_TOKO}, memakai default.\n{e}")
        return DEFAULT_DATABASE_FORM.copy()

def simpan_database_toko(data_toko):
    try:
        with open(FILE_KONFIG_TOKO, "w", encoding="utf-8") as f:
            json.dump(data_toko, f, indent=4)
    except Exception as e:
        messagebox.showerror("Error Simpan", f"Gagal menyimpan konfigurasi toko:\n{e}")

def muat_preferensi():
    if os.path.exists(FILE_PREFERENSI):
        try:
            with open(FILE_PREFERENSI, "r", encoding="utf-8") as f:
                return json.load(f)
        except Exception:
            pass
    return {}

def simpan_preferensi(toko, jenis, nama, posisi, headless, mode_sumber):
    try:
        with open(FILE_PREFERENSI, "w", encoding="utf-8") as f:
            json.dump({
                "toko": toko,
                "jenis": jenis,
                "nama": nama,
                "posisi": posisi,
                "headless": headless,
                "mode_sumber": mode_sumber
            }, f, indent=4)
    except Exception:
        pass

DATABASE_FORM = muat_database_toko()

# ==================== FUNGSI UTILITAS & LOGGING ====================
def bersihkan_angka(val):
    if pd.isna(val):
        return ''
    txt = str(val).strip()
    if txt.endswith('.0'):
        return txt[:-2]
    elif txt.lower() == 'nan':
        return ''
    return txt

def format_waktu(detik):
    detik = int(max(0, detik))
    menit, dtk = divmod(detik, 60)
    jam, menit = divmod(menit, 60)
    if jam > 0:
        return f"{jam}j {menit}m {dtk}d"
    elif menit > 0:
        return f"{menit}m {dtk}d"
    else:
        return f"{dtk} detik"

def tulis_log(pesan, tipe="INFO"):
    waktu_skrg = datetime.now().strftime("%H:%M:%S")
    baris_log = f"[{waktu_skrg}] {pesan}\n"
    
    def _update():
        kotak_log.config(state="normal")
        kotak_log.insert(tk.END, baris_log, tipe)
        kotak_log.see(tk.END)
        kotak_log.config(state="disabled")
        
    root.after(0, _update)

def cek_status_kontrol(nomor_data):
    global is_paused, is_stopped
    while is_paused and not is_stopped:
        label_status.config(
            text=f"⏸ Dijeda sebelum data ke-{nomor_data}. Klik 'LANJUTKAN'...", 
            fg="#FF9800"
        )
        root.update_idletasks()
        time.sleep(0.3)
    return is_stopped

# ==================== FUNGSI GANTI MODE SUMBER DATA (DINAMIS TANPA RUANG KOSONG) ====================
def ganti_mode_sumber():
    mode = var_mode_sumber.get()
    if mode == "file":
        panel_paste.pack_forget()
        panel_file.pack(fill="x", padx=6, pady=4)
    else:
        panel_file.pack_forget()
        panel_paste.pack(fill="both", expand=True, padx=6, pady=2)
        perbarui_nomor_urut_tabel()

# ==================== FUNGSI TABEL COPY-PASTE LANGSUNG ====================
def perbarui_nomor_urut_tabel():
    semua_item = tree_data.get_children()
    for idx, item_id in enumerate(semua_item, start=1):
        nilai_lama = tree_data.item(item_id, "values")
        tree_data.item(item_id, values=(idx, nilai_lama[1], nilai_lama[2], nilai_lama[3], nilai_lama[4]))
    
    total = len(semua_item)
    label_info_tabel.config(text=f"Total: {total} baris siap diinput")
    if var_mode_sumber.get() == "paste":
        spin_mulai.config(to=max(1, total))

def paste_dari_clipboard(event=None):
    if str(btn_start['state']) == 'disabled':
        return
    try:
        teks_clip = root.clipboard_get()
    except tk.TclError:
        messagebox.showwarning("Clipboard Kosong", "Tidak ada data di clipboard! Silakan blok dan Copy (Ctrl+C) sel di Excel terlebih dahulu.")
        return

    baris_mentah = teks_clip.splitlines()
    jumlah_masuk = 0

    for b in baris_mentah:
        if not b.strip():
            continue
        kolom = b.split('\t') if '\t' in b else b.split(';')
        kolom = [bersihkan_angka(k) for k in kolom]

        # Skip otomatis jika header ikut ter-copy
        if len(kolom) >= 2 and (kolom[0].lower() == "alamat keep stock" or kolom[1].lower() == "sku"):
            continue

        while len(kolom) < 4:
            kolom.append("")

        if not any(kolom[:4]):
            continue

        tree_data.insert("", tk.END, values=(0, kolom[0], kolom[1], kolom[2], kolom[3]))
        jumlah_masuk += 1

    perbarui_nomor_urut_tabel()
    if jumlah_masuk > 0:
        tulis_log(f"📋 Berhasil Paste {jumlah_masuk} baris data dari Excel ke tabel.", "SUKSES")
    else:
        messagebox.showinfo("Info Paste", "Tidak ada baris data valid yang ditemukan di clipboard.")

def tambah_baris_manual():
    v_keep = ent_m_keep.get().strip()
    v_sku = ent_m_sku.get().strip()
    v_qty = ent_m_qty.get().strip()
    v_gondola = ent_m_gondola.get().strip()

    if not any([v_keep, v_sku, v_qty, v_gondola]):
        messagebox.showwarning("Kosong", "Isi minimal salah satu kolom sebelum menekan tombol Tambah!")
        return

    tree_data.insert("", tk.END, values=(0, v_keep, v_sku, v_qty, v_gondola))
    perbarui_nomor_urut_tabel()

    ent_m_keep.delete(0, tk.END)
    ent_m_sku.delete(0, tk.END)
    ent_m_qty.delete(0, tk.END)
    ent_m_gondola.delete(0, tk.END)
    ent_m_keep.focus_set()

def hapus_baris_terpilih(event=None):
    if str(btn_start['state']) == 'disabled':
        return
    terpilih = tree_data.selection()
    if not terpilih:
        messagebox.showinfo("Pilih Baris", "Klik salah satu (atau beberapa) baris di tabel yang ingin dihapus!")
        return
    for item in terpilih:
        tree_data.delete(item)
    perbarui_nomor_urut_tabel()

def kosongkan_tabel():
    if not tree_data.get_children():
        return
    if messagebox.askyesno("Konfirmasi", "Kosongkan seluruh baris pada tabel input?"):
        for item in tree_data.get_children():
            tree_data.delete(item)
        perbarui_nomor_urut_tabel()
        tulis_log("🗑 Tabel input langsung telah dikosongkan.", "WARN")

def edit_sel_tabel(event):
    if str(btn_start['state']) == 'disabled':
        return
    region = tree_data.identify("region", event.x, event.y)
    if region != "cell":
        return
    kolom_id = tree_data.identify_column(event.x)
    baris_id = tree_data.identify_row(event.y)
    
    if not baris_id or kolom_id == "#1":
        return

    indeks_kolom = int(kolom_id.replace("#", "")) - 1
    x, y, lebar, tinggi = tree_data.bbox(baris_id, kolom_id)
    nilai_saat_ini = tree_data.item(baris_id, "values")[indeks_kolom]

    entry_edit = tk.Entry(tree_data, font=("Helvetica", 9))
    entry_edit.place(x=x, y=y, width=lebar, height=tinggi)
    entry_edit.insert(0, nilai_saat_ini)
    entry_edit.select_range(0, tk.END)
    entry_edit.focus_set()

    def simpan_edit(ev=None):
        if not entry_edit.winfo_exists():
            return
        nilai_baru = entry_edit.get().strip()
        nilai_baris = list(tree_data.item(baris_id, "values"))
        nilai_baris[indeks_kolom] = nilai_baru
        tree_data.item(baris_id, values=nilai_baris)
        entry_edit.destroy()

    entry_edit.bind("<Return>", simpan_edit)
    entry_edit.bind("<FocusOut>", simpan_edit)
    entry_edit.bind("<Escape>", lambda e: entry_edit.destroy())

def ambil_dataframe_dari_tabel():
    data_list = []
    for item_id in tree_data.get_children():
        vals = tree_data.item(item_id, "values")
        data_list.append({
            'Alamat Keep Stock': vals[1],
            'SKU': vals[2],
            'SKU QTY': vals[3],
            'Alamat Gondola': vals[4]
        })
    return pd.DataFrame(data_list, columns=KOLOM_WAJIB)

# ==================== FUNGSI UTAMA BOT SELENIUM ====================
def proses_selenium(df, link_form, nama_identitas, posisi_identitas, baris_mulai, mode_headless):
    global is_paused, is_stopped
    driver = None
    data_terakhir_sukses = baris_mulai - 1
    jumlah_berhasil_sesi_ini = 0
    total_data = len(df)

    try:
        mode_teks = "Headless" if mode_headless else "Tampil Layar"
        tulis_log(f"Menyiapkan Chrome [{mode_teks}]... (Baris {baris_mulai} s/d {total_data})", "INFO")
        
        opsi_chrome = Options()
        if mode_headless:
            opsi_chrome.add_argument("--headless")
        opsi_chrome.add_argument("--disable-gpu")
        opsi_chrome.add_argument("--window-size=1920,1080")
        opsi_chrome.add_experimental_option('excludeSwitches', ['enable-logging'])

        driver = webdriver.Chrome(options=opsi_chrome)
        wait = WebDriverWait(driver, 15)
        
        progress_bar["maximum"] = total_data
        progress_bar["value"] = baris_mulai - 1
        
        daftar_waktu_input = []
        sisa_awal = total_data - (baris_mulai - 1)
        label_eta.config(text=f"Target: {sisa_awal} baris | Estimasi sisa waktu: Menghitung...")

        perlu_buka_link_awal = True

        for index, row in df.iterrows():
            nomor_data = index + 1
            
            if nomor_data < baris_mulai:
                continue

            if cek_status_kontrol(nomor_data):
                break

            berhasil = False
            percobaan = 1
            waktu_mulai_baris = time.time()

            keep_saat_ini = bersihkan_angka(row['Alamat Keep Stock'])
            sku_saat_ini = bersihkan_angka(row['SKU'])
            qty_saat_ini = bersihkan_angka(row['SKU QTY'])
            gondola_saat_ini = bersihkan_angka(row['Alamat Gondola'])

            while not berhasil:
                if cek_status_kontrol(nomor_data):
                    break

                try:
                    if percobaan > 1:
                        label_status.config(
                            text=f"Mencoba ulang data {nomor_data} (Percobaan ke-{percobaan})...", 
                            fg="#FF9800"
                        )
                        perlu_buka_link_awal = True
                    else:
                        label_status.config(
                            text=f"Memproses data {nomor_data} dari {total_data} (SKU: {sku_saat_ini})...", 
                            fg="#2196F3"
                        )
                    
                    progress_bar["value"] = index + 0.5
                    root.update_idletasks()

                    if perlu_buka_link_awal:
                        driver.get(link_form)

                    # --- LAYER 1 (IDENTITAS) ---
                    kolom_nama = wait.until(EC.element_to_be_clickable((By.XPATH, '(//input[@type="text"])[1]')))
                    kolom_nama.clear()
                    kolom_nama.send_keys(nama_identitas)
                    
                    dropdown_posisi = wait.until(EC.element_to_be_clickable((By.XPATH, "(//div[@role='listbox'])[1]")))
                    dropdown_posisi.click()
                    
                    opsi_posisi = wait.until(EC.element_to_be_clickable((By.XPATH, f"//div[@role='option']//span[text()='{posisi_identitas}']")))
                    time.sleep(0.3)
                    opsi_posisi.click()
                    
                    time.sleep(0.3)
                    tombol_next = wait.until(EC.element_to_be_clickable((By.XPATH, "//span[text()='Berikutnya' or text()='Next']")))
                    tombol_next.click()

                    # --- LAYER 2 (DATA STOK) ---
                    kolom_gondola = wait.until(EC.element_to_be_clickable((By.XPATH, '(//input[@type="text"])[4]')))

                    kolom_keep = driver.find_element(By.XPATH, '(//input[@type="text"])[1]')
                    kolom_keep.clear()
                    kolom_keep.send_keys(keep_saat_ini)

                    kolom_sku = driver.find_element(By.XPATH, '(//input[@type="text"])[2]')
                    kolom_sku.clear()
                    kolom_sku.send_keys(sku_saat_ini)

                    kolom_qty = driver.find_element(By.XPATH, '(//input[@type="text"])[3]')
                    kolom_qty.clear()
                    kolom_qty.send_keys(qty_saat_ini)

                    kolom_gondola.clear()
                    kolom_gondola.send_keys(gondola_saat_ini)

                    if is_stopped:
                        break

                    tombol_kirim = wait.until(EC.element_to_be_clickable((By.XPATH, "//span[text()='Kirim' or text()='Submit']")))
                    tombol_kirim.click()
                    
                    wait.until(EC.invisibility_of_element_located((By.XPATH, "//span[text()='Kirim' or text()='Submit']")))
                    
                    try:
                        link_lagi = WebDriverWait(driver, 4).until(
                            EC.element_to_be_clickable((By.XPATH, "//a[contains(@href, 'viewform') or contains(@href, 'form_confirm')]"))
                        )
                        link_lagi.click()
                        perlu_buka_link_awal = False
                    except Exception:
                        perlu_buka_link_awal = True

                    berhasil = True
                    data_terakhir_sukses = nomor_data
                    jumlah_berhasil_sesi_ini += 1
                    progress_bar["value"] = nomor_data

                    durasi_baris = time.time() - waktu_mulai_baris
                    daftar_waktu_input.append(durasi_baris)
                    rata_rata_waktu = sum(daftar_waktu_input) / len(daftar_waktu_input)
                    sisa_baris = total_data - nomor_data
                    estimasi_detik = sisa_baris * rata_rata_waktu

                    if sisa_baris > 0:
                        label_eta.config(
                            text=f"Kecepatan: {durasi_baris:.1f} dtk/baris | Sisa: {sisa_baris} baris | ETA: ~{format_waktu(estimasi_detik)}"
                        )
                    else:
                        label_eta.config(text="Sisa: 0 baris | Estimasi sisa waktu: Selesai!")

                    root.update_idletasks()
                    tulis_log(f"✓ Baris {nomor_data}/{total_data} SUKSES -> SKU: {sku_saat_ini} (Qty: {qty_saat_ini}) [{durasi_baris:.1f}s]", "SUKSES")

                except Exception as e:
                    if is_stopped:
                        break
                    pesan_err = str(e).split('\n')[0][:60]
                    tulis_log(f"⚠ Gagal baris {nomor_data} (Percobaan {percobaan}): {pesan_err}...", "ERROR")
                    percobaan += 1
                    perlu_buka_link_awal = True
                    time.sleep(2)

            if is_stopped:
                break

        driver.quit()

        if is_stopped:
            baris_lanjut = data_terakhir_sukses + 1
            label_status.config(
                text=f"⏹ Dihentikan. Terakhir sukses: baris ke-{data_terakhir_sukses} dari {total_data}.", 
                fg="#F44336"
            )
            tulis_log(f"⏹ Bot dihentikan pengguna. Untuk lanjut, mulai dari baris ke-{baris_lanjut}.", "WARN")
            
            if baris_lanjut <= total_data:
                spin_mulai.config(state="normal")
                spin_mulai.delete(0, tk.END)
                spin_mulai.insert(0, str(baris_lanjut))

            messagebox.showinfo(
                "Proses Dihentikan", 
                f"Bot telah dihentikan.\n\n"
                f"• Berhasil diinput sesi ini: {jumlah_berhasil_sesi_ini} baris\n"
                f"• Baris terakhir sukses: Baris ke-{data_terakhir_sukses}\n\n"
                f"Kolom 'Mulai dari data ke' telah otomatis diatur ke angka {baris_lanjut}."
            )
        else:
            label_status.config(text="Selesai! Semua data berhasil diinput tanpa terlewat.", fg="green")
            tulis_log(f"🎉 SELESAI! Total {jumlah_berhasil_sesi_ini} baris data berhasil diinput.", "SUKSES")
            spin_mulai.config(state="normal")
            spin_mulai.delete(0, tk.END)
            spin_mulai.insert(0, "1")
            messagebox.showinfo("Berhasil", "Semua data berhasil diinput ke Google Form!")

    except Exception as error:
        label_status.config(text="Terjadi kesalahan sistem.", fg="red")
        tulis_log(f"❌ ERROR SISTEM: {error}", "ERROR")
        messagebox.showerror("Error Sistem", f"Terjadi kesalahan saat menjalankan browser:\n{error}")
        try:
            driver.quit()
        except:
            pass

    finally:
        reset_tombol()

# ==================== JENDELA KELOLA TOKO DINAMIS ====================
def buka_jendela_kelola_toko():
    win = tk.Toplevel(root)
    win.title("Kelola Database Toko (config_toko.json)")
    win.geometry("500x410")
    win.resizable(False, False)
    win.transient(root)
    win.grab_set()

    tk.Label(win, text="DAFTAR CABANG TOKO & LINK FORM", font=("Helvetica", 11, "bold")).pack(pady=10)

    frame_atas = tk.Frame(win, padx=15)
    frame_atas.pack(fill="x")

    tk.Label(frame_atas, text="Toko Terdaftar:", font=("Helvetica", 9, "bold")).pack(anchor="w")
    
    listbox_toko = tk.Listbox(frame_atas, height=5, font=("Helvetica", 10))
    listbox_toko.pack(fill="x", pady=4)

    def refresh_listbox():
        listbox_toko.delete(0, tk.END)
        for kode in DATABASE_FORM.keys():
            listbox_toko.insert(tk.END, kode)
        daftar_baru = list(DATABASE_FORM.keys())
        combo_toko["values"] = daftar_baru
        if combo_toko.get() not in daftar_baru:
            combo_toko.set("Pilih Toko")

    refresh_listbox()

    frame_edit = tk.LabelFrame(win, text=" Tambah / Update Toko ", font=("Helvetica", 9, "bold"), padx=10, pady=10)
    frame_edit.pack(fill="x", padx=15, pady=8)

    tk.Label(frame_edit, text="Kode Toko :").grid(row=0, column=0, sticky="w", pady=3)
    ent_kode = tk.Entry(frame_edit, width=20, font=("Helvetica", 9))
    ent_kode.grid(row=0, column=1, sticky="w", pady=3, padx=5)
    tk.Label(frame_edit, text="(Contoh: XBTK, XBDJ)", fg="gray", font=("Helvetica", 8)).grid(row=0, column=1, sticky="e")

    tk.Label(frame_edit, text="Link Form IN :").grid(row=1, column=0, sticky="w", pady=3)
    ent_in = tk.Entry(frame_edit, width=48, font=("Helvetica", 9))
    ent_in.grid(row=1, column=1, pady=3, padx=5)

    tk.Label(frame_edit, text="Link Form OUT :").grid(row=2, column=0, sticky="w", pady=3)
    ent_out = tk.Entry(frame_edit, width=48, font=("Helvetica", 9))
    ent_out.grid(row=2, column=1, pady=3, padx=5)

    def saat_pilih_toko(event):
        pilihan = listbox_toko.curselection()
        if pilihan:
            kode = listbox_toko.get(pilihan[0])
            ent_kode.delete(0, tk.END)
            ent_kode.insert(0, kode)
            ent_in.delete(0, tk.END)
            ent_in.insert(0, DATABASE_FORM[kode].get("IN", ""))
            ent_out.delete(0, tk.END)
            ent_out.insert(0, DATABASE_FORM[kode].get("OUT", ""))

    listbox_toko.bind("<<ListboxSelect>>", saat_pilih_toko)

    def aksi_simpan_toko():
        kode = ent_kode.get().strip().upper()
        link_in = ent_in.get().strip()
        link_out = ent_out.get().strip()

        if not kode or not link_in or not link_out:
            messagebox.showwarning("Data Belum Lengkap", "Kode Toko, Link IN, dan Link OUT wajib diisi semua!", parent=win)
            return

        DATABASE_FORM[kode] = {"IN": link_in, "OUT": link_out}
        simpan_database_toko(DATABASE_FORM)
        refresh_listbox()
        tulis_log(f"⚙ Konfigurasi toko '{kode}' berhasil disimpan ke {FILE_KONFIG_TOKO}.", "INFO")
        messagebox.showinfo("Tersimpan", f"Data toko '{kode}' berhasil disimpan!", parent=win)

    def aksi_hapus_toko():
        pilihan = listbox_toko.curselection()
        if not pilihan:
            messagebox.showwarning("Pilih Toko", "Klik salah satu toko di daftar atas yang ingin dihapus!", parent=win)
            return
        kode = listbox_toko.get(pilihan[0])
        if len(DATABASE_FORM) <= 1:
            messagebox.showwarning("Tidak Bisa Dihapus", "Minimal harus ada 1 toko tersisa di database!", parent=win)
            return
        if messagebox.askyesno("Konfirmasi Hapus", f"Yakin ingin menghapus toko '{kode}'?", parent=win):
            del DATABASE_FORM[kode]
            simpan_database_toko(DATABASE_FORM)
            refresh_listbox()
            ent_kode.delete(0, tk.END)
            ent_in.delete(0, tk.END)
            ent_out.delete(0, tk.END)
            tulis_log(f"⚙ Toko '{kode}' dihapus dari konfigurasi.", "WARN")

    frame_btn_toko = tk.Frame(win)
    frame_btn_toko.pack(pady=8)

    tk.Button(frame_btn_toko, text="💾 Simpan / Update Toko", bg="#4CAF50", fg="white", font=("Helvetica", 9, "bold"), padx=10, command=aksi_simpan_toko).grid(row=0, column=0, padx=5)
    tk.Button(frame_btn_toko, text="🗑 Hapus Toko Terpilih", bg="#F44336", fg="white", font=("Helvetica", 9, "bold"), padx=10, command=aksi_hapus_toko).grid(row=0, column=1, padx=5)

# ==================== FUNGSI TOMBOL GUI UTAMA ====================
def pilih_file():
    global filepath_excel
    file_terpilih = filedialog.askopenfilename(
        title="Pilih File Excel Keep Stock",
        filetypes=[("Excel Files", "*.xlsx *.xls")]
    )
    if file_terpilih:
        filepath_excel = file_terpilih
        nama_file = os.path.basename(file_terpilih)
        
        try:
            df_cek = pd.read_excel(filepath_excel)
            jml_baris = len(df_cek)
            label_file_terpilih.config(text=f"{nama_file} ({jml_baris} baris)", fg="green")
            spin_mulai.config(to=max(1, jml_baris))
            tulis_log(f"📁 File dimuat: {nama_file} (Total: {jml_baris} baris data).", "INFO")
        except Exception:
            label_file_terpilih.config(text=f"File: {nama_file}", fg="green")

def mulai_klik():
    global is_paused, is_stopped
    toko = combo_toko.get()
    jenis = combo_jenis.get()
    nama = entry_nama.get().strip()
    posisi = combo_posisi.get()
    mode_headless = var_headless.get()
    mode_sumber = var_mode_sumber.get()

    if mode_sumber == "file":
        if not filepath_excel:
            messagebox.showwarning("Peringatan", "Silakan pilih file Excel terlebih dahulu, atau ganti ke mode 'Copy-Paste / Isi Tabel Langsung'!")
            return
        try:
            df_input = pd.read_excel(filepath_excel)
        except Exception as e:
            messagebox.showerror("Error Excel", f"Gagal membaca file Excel!\nDetail: {e}")
            return
        for kol in KOLOM_WAJIB:
            if kol not in df_input.columns:
                messagebox.showerror("Format Excel Salah", f"Kolom '{kol}' tidak ditemukan di file Excel!")
                return
    else:
        df_input = ambil_dataframe_dari_tabel()
        if len(df_input) == 0:
            messagebox.showwarning("Tabel Kosong", "Tabel Input Langsung masih kosong!\nSilakan Paste (Ctrl+V) data dari Excel atau tambahkan baris manual.")
            return

    total_data = len(df_input)
    if total_data == 0:
        messagebox.showwarning("Data Kosong", "Sumber data yang dipilih memiliki 0 baris!")
        return

    if toko == "Pilih Toko" or toko not in DATABASE_FORM or jenis == "Jenis Form" or not nama or posisi == "Pilih Posisi":
        messagebox.showwarning("Peringatan", "Semua kolom pilihan Toko, Jenis Form, dan Identitas PIC wajib diisi!")
        return

    try:
        baris_mulai = int(spin_mulai.get().strip())
        if baris_mulai < 1 or baris_mulai > total_data:
            messagebox.showwarning("Baris Tidak Valid", f"Mulai dari data ke-{baris_mulai} tidak valid (Total data: {total_data} baris)!")
            return
    except ValueError:
        messagebox.showwarning("Input Baris Salah", "Kolom 'Mulai dari data ke' harus berupa angka bulat!")
        return

    baris_bolong = []
    for idx in range(baris_mulai - 1, total_data):
        r = df_input.iloc[idx]
        if any(bersihkan_angka(r[k]) == '' for k in KOLOM_WAJIB):
            baris_bolong.append(str(idx + 1))

    if baris_bolong:
        contoh_baris = ", ".join(baris_bolong[:5]) + ("..." if len(baris_bolong) > 5 else "")
        lanjut = messagebox.askyesno(
            "Peringatan Data Kosong",
            f"Ditemukan {len(baris_bolong)} baris yang memiliki kolom kosong (pada baris ke: {contoh_baris}).\n\n"
            f"Jika kolom di Google Form bersifat Wajib Diisi, bot mungkin akan tertahan di baris tersebut.\n"
            f"Tetap lanjutkan?"
        )
        if not lanjut:
            return

    simpan_preferensi(toko, jenis, nama, posisi, mode_headless, mode_sumber)
    
    is_paused = False
    is_stopped = False
        
    btn_start.config(state="disabled", text="Memproses...", bg="#9E9E9E")
    btn_pilih.config(state="disabled")
    btn_kelola_toko.config(state="disabled")
    spin_mulai.config(state="disabled")
    rb_mode_file.config(state="disabled")
    rb_mode_paste.config(state="disabled")
    btn_pause.config(state="normal", text="⏸ PAUSE", bg="#FF9800")
    btn_stop.config(state="normal", bg="#F44336")
    
    link_otomatis = DATABASE_FORM[toko][jenis]
    
    progress_bar["value"] = 0
    label_status.config(text="Menyiapkan browser...", fg="blue")
    
    thread = threading.Thread(
        target=proses_selenium, 
        args=(df_input, link_otomatis, nama, posisi, baris_mulai, mode_headless)
    )
    thread.daemon = True
    thread.start()

def pause_klik():
    global is_paused
    if not is_paused:
        is_paused = True
        btn_pause.config(text="▶ LANJUTKAN", bg="#2196F3")
        label_status.config(text="Menunggu baris saat ini selesai untuk menjeda...", fg="#FF9800")
        tulis_log("⏸ Perintah Pause diterima. Menunggu baris aktif selesai...", "WARN")
    else:
        is_paused = False
        btn_pause.config(text="⏸ PAUSE", bg="#FF9800")
        label_status.config(text="Melanjutkan proses...", fg="#2196F3")
        tulis_log("▶ Proses dilanjutkan kembali.", "INFO")

def stop_klik():
    global is_stopped, is_paused
    if messagebox.askyesno("Konfirmasi Stop", "Apakah Anda yakin ingin menghentikan proses bot sepenuhnya?"):
        is_stopped = True
        is_paused = False
        btn_pause.config(state="disabled")
        btn_stop.config(state="disabled")
        label_status.config(text="Menghentikan bot dan menutup browser...", fg="red")
        tulis_log("⏹ Menghentikan bot...", "ERROR")

def reset_tombol():
    global is_paused, is_stopped
    is_paused = False
    is_stopped = False
    btn_start.config(state="normal", text="▶ JALANKAN BOT", bg="#4CAF50")
    btn_pilih.config(state="normal")
    btn_kelola_toko.config(state="normal")
    spin_mulai.config(state="normal")
    rb_mode_file.config(state="normal")
    rb_mode_paste.config(state="normal")
    btn_pause.config(state="disabled", text="⏸ PAUSE", bg="#E0E0E0")
    btn_stop.config(state="disabled", bg="#E0E0E0")

def bersihkan_log():
    kotak_log.config(state="normal")
    kotak_log.delete(1.0, tk.END)
    kotak_log.config(state="disabled")

# ==================== TAMPILAN UTAMA GUI (COMPACT & ANTI-TERPOTONG) ====================
root = tk.Tk()
root.title("Bot Keep Stock Otomatis v5.9 (Direct Excel Copy-Paste & Smart Table)")
root.geometry("680x640")
root.minsize(640, 580)

# Menempatkan jendela agak ke atas agar tidak tertutup Taskbar Windows di layar 1366x768
lebar_layar = root.winfo_screenwidth()
tinggi_layar = root.winfo_screenheight()
pos_x = max(0, (lebar_layar - 680) // 2)
pos_y = max(5, (tinggi_layar - 680) // 2)
root.geometry(f"680x640+{pos_x}+{pos_y}")

label_judul = tk.Label(root, text="AUTO UPDATE KEEP STOCK v5.9", font=("Helvetica", 13, "bold"))
label_judul.pack(pady=(6, 2))

# --- LANGKAH 1: SUMBER DATA DINAMIS (MENYUSUT OTOMATIS JIKA PILIH FILE EXCEL) ---
frame_sumber = tk.LabelFrame(root, text=" Langkah 1: Pilih Sumber Data ", font=("Helvetica", 9, "bold"), padx=8, pady=4)
frame_sumber.pack(pady=3, fill="x", padx=14)

frame_pilihan_mode = tk.Frame(frame_sumber)
frame_pilihan_mode.pack(fill="x", pady=(0, 4))

var_mode_sumber = tk.StringVar(value="file")

rb_mode_file = tk.Radiobutton(
    frame_pilihan_mode, text="📁 Pilih File Excel", variable=var_mode_sumber, value="file",
    font=("Helvetica", 9, "bold"), fg="#1565C0", command=ganti_mode_sumber
)
rb_mode_file.pack(side="left", padx=(4, 15))

rb_mode_paste = tk.Radiobutton(
    frame_pilihan_mode, text="📋 Copy-Paste / Isi Tabel Langsung", variable=var_mode_sumber, value="paste",
    font=("Helvetica", 9, "bold"), fg="#2E7D32", command=ganti_mode_sumber
)
rb_mode_paste.pack(side="left", padx=4)

ttk.Separator(frame_sumber, orient="horizontal").pack(fill="x", pady=2)

# >> PANEL A: UPLOAD FILE EXCEL (Ramping, Tanpa Ruang Kosong) <<
panel_file = tk.Frame(frame_sumber)

btn_pilih = tk.Button(panel_file, text="Cari File Excel...", command=pilih_file, bg="#2196F3", fg="white", font=("Helvetica", 9, "bold"), padx=10, pady=2)
btn_pilih.pack(side="left", padx=4, pady=3)

label_file_terpilih = tk.Label(panel_file, text="Belum ada file yang dipilih", fg="red", font=("Helvetica", 9, "italic"))
label_file_terpilih.pack(side="left", padx=8)

# >> PANEL B: COPY-PASTE & TABEL LANGSUNG <<
panel_paste = tk.Frame(frame_sumber)

frame_toolbar_tabel = tk.Frame(panel_paste)
frame_toolbar_tabel.pack(fill="x", pady=2)

btn_paste_clip = tk.Button(
    frame_toolbar_tabel, text="📋 Paste dari Excel (Ctrl+V)", 
    bg="#1976D2", fg="white", font=("Helvetica", 8, "bold"), padx=6, command=paste_dari_clipboard
)
btn_paste_clip.pack(side="left", padx=2)

btn_hapus_baris = tk.Button(
    frame_toolbar_tabel, text="✖ Hapus Baris (Del)", 
    bg="#E53935", fg="white", font=("Helvetica", 8, "bold"), padx=6, command=hapus_baris_terpilih
)
btn_hapus_baris.pack(side="left", padx=4)

btn_reset_tabel = tk.Button(
    frame_toolbar_tabel, text="🗑 Kosongkan", 
    bg="#757575", fg="white", font=("Helvetica", 8, "bold"), padx=6, command=kosongkan_tabel
)
btn_reset_tabel.pack(side="left", padx=2)

label_info_tabel = tk.Label(frame_toolbar_tabel, text="Total: 0 baris siap diinput", font=("Helvetica", 8, "bold"), fg="#2E7D32")
label_info_tabel.pack(side="right", padx=4)

# Baris Input Manual Cepat
frame_manual = tk.Frame(panel_paste, pady=2)
frame_manual.pack(fill="x")

tk.Label(frame_manual, text="Keep:", font=("Helvetica", 8)).pack(side="left")
ent_m_keep = tk.Entry(frame_manual, width=12, font=("Helvetica", 8))
ent_m_keep.pack(side="left", padx=2)

tk.Label(frame_manual, text="SKU:", font=("Helvetica", 8)).pack(side="left", padx=(4, 0))
ent_m_sku = tk.Entry(frame_manual, width=14, font=("Helvetica", 8))
ent_m_sku.pack(side="left", padx=2)

tk.Label(frame_manual, text="QTY:", font=("Helvetica", 8)).pack(side="left", padx=(4, 0))
ent_m_qty = tk.Entry(frame_manual, width=8, font=("Helvetica", 8))
ent_m_qty.pack(side="left", padx=2)

tk.Label(frame_manual, text="Gondola:", font=("Helvetica", 8)).pack(side="left", padx=(4, 0))
ent_m_gondola = tk.Entry(frame_manual, width=12, font=("Helvetica", 8))
ent_m_gondola.pack(side="left", padx=2)

btn_tambah_m = tk.Button(frame_manual, text="+ Tambah", bg="#43A047", fg="white", font=("Helvetica", 8, "bold"), padx=6, command=tambah_baris_manual)
btn_tambah_m.pack(side="left", padx=6)

# Tabel Treeview Ber-Header (Tinggi disetel 4 baris agar hemat ruang vertikal)
frame_tree = tk.Frame(panel_paste)
frame_tree.pack(fill="both", expand=True, pady=2)

kolom_tree = ("No", "Alamat Keep Stock", "SKU", "SKU QTY", "Alamat Gondola")
tree_data = ttk.Treeview(frame_tree, columns=kolom_tree, show="headings", height=4, selectmode="extended")

tree_data.heading("No", text="No")
tree_data.heading("Alamat Keep Stock", text="Alamat Keep Stock")
tree_data.heading("SKU", text="SKU")
tree_data.heading("SKU QTY", text="SKU QTY")
tree_data.heading("Alamat Gondola", text="Alamat Gondola")

tree_data.column("No", width=35, anchor="center")
tree_data.column("Alamat Keep Stock", width=140, anchor="center")
tree_data.column("SKU", width=140, anchor="center")
tree_data.column("SKU QTY", width=80, anchor="center")
tree_data.column("Alamat Gondola", width=140, anchor="center")

scroll_tree = ttk.Scrollbar(frame_tree, orient="vertical", command=tree_data.yview)
tree_data.configure(yscrollcommand=scroll_tree.set)

tree_data.pack(side="left", fill="both", expand=True)
scroll_tree.pack(side="right", fill="y")

tk.Label(
    panel_paste, 
    text="💡 Tips: Blok 4 kolom data di Excel -> Copy (Ctrl+C) -> Klik Paste di atas. Double-click sel tabel untuk edit.", 
    font=("Helvetica", 7, "italic"), fg="#555555"
).pack(anchor="w")

tree_data.bind("<Double-1>", edit_sel_tabel)
tree_data.bind("<Control-v>", paste_dari_clipboard)
tree_data.bind("<Control-V>", paste_dari_clipboard)
tree_data.bind("<Delete>", hapus_baris_terpilih)
ent_m_gondola.bind("<Return>", lambda e: tambah_baris_manual())

# --- LANGKAH 2: LAYOUT 2 KOLOM SEJAJAR (HEMAT TEMPAT VERTIKAL) ---
frame_input = tk.LabelFrame(root, text=" Langkah 2: Pilihan Toko, Identitas PIC & Pengaturan ", font=("Helvetica", 9, "bold"), padx=10, pady=4)
frame_input.pack(pady=3, fill="x", padx=14)

# Kolom Kiri (Pilih Toko, Jenis Form, Mulai Data)
tk.Label(frame_input, text="Pilih Toko :", font=("Helvetica", 9)).grid(row=0, column=0, padx=4, pady=3, sticky="w")
frame_baris_toko = tk.Frame(frame_input)
frame_baris_toko.grid(row=0, column=1, padx=4, pady=3, sticky="w")

daftar_toko = list(DATABASE_FORM.keys())
combo_toko = ttk.Combobox(frame_baris_toko, values=daftar_toko, width=12, state="readonly", font=("Helvetica", 9))
combo_toko.set("Pilih Toko")
combo_toko.pack(side="left")

btn_kelola_toko = tk.Button(frame_baris_toko, text="⚙ Kelola Toko", font=("Helvetica", 8, "bold"), bg="#607D8B", fg="white", command=buka_jendela_kelola_toko)
btn_kelola_toko.pack(side="left", padx=5)

tk.Label(frame_input, text="Jenis Form :", font=("Helvetica", 9)).grid(row=1, column=0, padx=4, pady=3, sticky="w")
combo_jenis = ttk.Combobox(frame_input, values=["IN", "OUT"], width=23, state="readonly", font=("Helvetica", 9))
combo_jenis.set("Jenis Form")
combo_jenis.grid(row=1, column=1, padx=4, pady=3, sticky="w")

tk.Label(frame_input, text="Mulai data ke :", font=("Helvetica", 9, "bold"), fg="#1976D2").grid(row=2, column=0, padx=4, pady=3, sticky="w")
spin_mulai = tk.Spinbox(frame_input, from_=1, to=99999, width=8, font=("Helvetica", 9, "bold"))
spin_mulai.grid(row=2, column=1, padx=4, pady=3, sticky="w")

# Kolom Kanan (Nama PIC, Posisi, Checkbox Headless)
tk.Label(frame_input, text="Nama PIC :", font=("Helvetica", 9)).grid(row=0, column=2, padx=(18, 4), pady=3, sticky="w")
entry_nama = tk.Entry(frame_input, width=25, font=("Helvetica", 9))
entry_nama.grid(row=0, column=3, padx=4, pady=3, sticky="w")

tk.Label(frame_input, text="Posisi :", font=("Helvetica", 9)).grid(row=1, column=2, padx=(18, 4), pady=3, sticky="w")
pilihan_posisi = ["RM", "AM", "DM", "BM", "ABM", "MP", "AMP"]
combo_posisi = ttk.Combobox(frame_input, values=pilihan_posisi, width=23, state="readonly", font=("Helvetica", 9))
combo_posisi.set("Pilih Posisi")
combo_posisi.grid(row=1, column=3, padx=4, pady=3, sticky="w")

var_headless = tk.BooleanVar(value=True)
chk_headless = tk.Checkbutton(
    frame_input, text="Sembunyikan Browser (Headless)", 
    variable=var_headless, font=("Helvetica", 8)
)
chk_headless.grid(row=2, column=2, columnspan=2, padx=(14, 4), pady=2, sticky="w")

# --- STATUS & PROGRESS BAR ---
frame_progress = tk.LabelFrame(root, text=" Status Pergerakan Bot & Estimasi Waktu ", font=("Helvetica", 9, "bold"), padx=10, pady=4)
frame_progress.pack(pady=3, fill="x", padx=14)

frame_info_prog = tk.Frame(frame_progress)
frame_info_prog.pack(fill="x")

label_status = tk.Label(frame_info_prog, text="Bot siap dijalankan.", font=("Helvetica", 8, "italic"), fg="gray")
label_status.pack(side="left", padx=2)

label_eta = tk.Label(frame_info_prog, text="Estimasi sisa waktu: -", font=("Helvetica", 8, "bold"), fg="#555555")
label_eta.pack(side="right", padx=2)

progress_bar = ttk.Progressbar(frame_progress, orient="horizontal", mode="determinate")
progress_bar.pack(padx=2, pady=3, fill="x")

# --- PANEL TOMBOL KONTROL ---
frame_kontrol = tk.Frame(root)
frame_kontrol.pack(pady=4)

btn_start = tk.Button(frame_kontrol, text="▶ JALANKAN BOT", font=("Helvetica", 9, "bold"), bg="#4CAF50", fg="white", width=16, command=mulai_klik)
btn_start.grid(row=0, column=0, padx=4, ipady=3)

btn_pause = tk.Button(frame_kontrol, text="⏸ PAUSE", font=("Helvetica", 9, "bold"), bg="#E0E0E0", fg="white", width=12, state="disabled", command=pause_klik)
btn_pause.grid(row=0, column=1, padx=4, ipady=3)

btn_stop = tk.Button(frame_kontrol, text="⏹ STOP", font=("Helvetica", 9, "bold"), bg="#E0E0E0", fg="white", width=10, state="disabled", command=stop_klik)
btn_stop.grid(row=0, column=2, padx=4, ipady=3)

# --- KOTAK LIVE LOG ---
frame_log = tk.LabelFrame(root, text=" Live Log Aktivitas ", font=("Helvetica", 9, "bold"), padx=8, pady=4)
frame_log.pack(pady=(2, 6), fill="both", expand=True, padx=14)

header_log = tk.Frame(frame_log)
header_log.pack(fill="x")
btn_clear_log = tk.Button(header_log, text="Bersihkan Log", font=("Helvetica", 7), command=bersihkan_log)
btn_clear_log.pack(side="right", pady=(0, 2))

kotak_log = ScrolledText(frame_log, height=5, font=("Consolas", 8), state="disabled", bg="#1E1E1E", fg="#D4D4D4")
kotak_log.pack(fill="both", expand=True)

kotak_log.tag_config("SUKSES", foreground="#4CAF50")
kotak_log.tag_config("ERROR", foreground="#F44336")
kotak_log.tag_config("WARN", foreground="#FF9800")
kotak_log.tag_config("INFO", foreground="#64B5F6")

# Muat Preferensi Terakhir Pengguna (Auto-Fill)
prefs = muat_preferensi()
if prefs.get("toko") in daftar_toko:
    combo_toko.set(prefs["toko"])
if prefs.get("jenis") in ["IN", "OUT"]:
    combo_jenis.set(prefs["jenis"])
if prefs.get("nama"):
    entry_nama.insert(0, prefs["nama"])
if prefs.get("posisi") in pilihan_posisi:
    combo_posisi.set(prefs["posisi"])
if "headless" in prefs:
    var_headless.set(bool(prefs["headless"]))
if prefs.get("mode_sumber") in ["file", "paste"]:
    var_mode_sumber.set(prefs["mode_sumber"])

# Tampilkan panel sesuai mode aktif
ganti_mode_sumber()
tulis_log("Sistem v5.9 siap. Pilih mode 'Pilih File Excel' atau 'Copy-Paste Tabel' di Langkah 1.", "INFO")

root.mainloop()
