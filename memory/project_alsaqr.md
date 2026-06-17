---
name: project-alsaqr
description: Kullanıcının Academic Research Assistant olarak İtalya'da yapacağı Carfield SoC Linux kernel driver stajı — 22 Haziran 2026 başlangıç
metadata: 
  node_type: memory
  type: project
  originSessionId: 70dc4cca-5926-4173-bb05-28d4f9b9e6d5
---

**Staj detayı:**
- **Kurum:** UNIBO / ECS Lab, İtalya
- **Başlangıç:** 22 Haziran 2026
- **Rol:** Academic Research Assistant
- **Görev:** Carfield SoC üzerinde encrypted TinyML inference çalıştıran Linux kernel driver yazmak (%80 driver, %20 entegrasyon)

---

## Ekip

| Kişi | Unvan | Sorumluluk |
|---|---|---|
| Luca Barbierato | Asst. Professor | Proje yönetimi / danışman |
| Francesco Barchi | Asst. Professor | Proje yönetimi / danışman |
| Giovanni | PhD öğrencisi | **Bare metal tarafı** (PULP cluster) |
| Daniele | PhD öğrencisi | **Bare metal tarafı** (PULP cluster) |
| Tina | Yüksek lisans öğrencisi | Neural Network & Encryption tarafı |
| Kullanıcı | Academic Research Asst. | Linux kernel driver |

**İş bölümü:**
- Giovanni + Daniele → bare metal (PULP cluster üzerinde çalışan firmware)
- Luca + Francesco + Tina → Neural Network modeli + encryption/decryption logic
- Kullanıcı → Linux kernel driver (bu ikisini birbirine bağlayan katman)

---

## Platform: Carfield SoC

```
Carfield SoC (AlSaqr'ın gelişmiş versiyonu, PULP ekosistemi)
├── CVA6 (host CPU) — Linux çalışıyor
├── PULP Cluster — TinyML inference (multi-core, paralel)
└── OpenTitan (security island) — weight decryption, key management
```

**Güvenlik fikri:** Host (Linux) encrypted NN weights'i hiçbir zaman plaintext görmez.
`Encrypted weights → OpenTitan (decrypt) → PULP (inference) → CVA6 (sonuç)`

---

## Kernel Driver'ın Yapacakları

1. User space'den (Python) encrypted NN blob'u al (`/dev/carfield` device üzerinden)
2. PULP cluster'a yükle
3. OpenTitan'ı decryption için başlat
4. Virtual → Physical address translation yap (KRİTİK)
5. Execution'ı yönet, sonucu user space'e döndür

**İletişim:** Python (ctypes/cffi) → ioctl → kernel driver → Mailbox → PULP/OpenTitan

---

## Referans Repolar (Lokalinde)

### 1. `alsaqr-fpga-ecs` — Ana referans
- `develop/titanssl/titanssl_driver/driver.c` — Kernel driver (ANA REFERANS)
- `develop/titanssl/titanssl_driver/titanssl.h` — Paylaşılan struct'lar
- `develop/titanssl/titanssl_engine/engine.c` — OpenSSL engine (user space)
- `develop/titanssl/titanssl_firmware/` — OpenTitan firmware

### 2. `supt-openssl-ecslab` (alsaqr-fpga-ecs içinde)
- `driver/driver_core.c` — Eski driver
- `driver/kthread.c` — FPGA yokken OpenTitan simülasyonu (TEST İÇİN kritik!)
- `tests/ioctl_test.c` — User space test şablonu

---

## Mailbox Adresleri (AlSaqr referansı, Carfield'da değişebilir)

| Register | Adres |
|---|---|
| MBOX (mesaj) | 0x10404000 |
| DOORBELL | 0x10404020 |
| COMPLETION | 0x10404024 |

**Mesaj formatı:** 5 Word (20 byte) — MAGIC, HEADER_PHYS_ADDR, CMD+BITFIELD, SESSION, PID

---

## Kritik Teknik Notlar

1. **`fence.i` zorunlu** — Cache flush yapılmazsa donanım eski veriyi okur
2. **Fiziksel adres zorunlu** — Mailbox'a `page_to_phys()` ile çevrilmiş adres yazılmalı
3. **32-bit adres kısıtı** — OpenTitan 32-bit adres alanında çalışıyor
4. **Multi-page indirection** — Sanal adreste ardışık bellek fiziksel olarak dağınık olabilir; header page + map page yapısı ile çözülüyor

---

## Yol Haritası ve İlerleme

- **Aşama 0** ✅ TAMAMLANDI — `/dev/carfield`, `CARFIELD_PING` IOCTL, test geçti
- **Aşama 1** ✅ TAMAMLANDI — Carfield repo analiz edildi, memory map ve boot prosedürü çıkarıldı
- **Aşama 2** ✅ TAMAMLANDI — `mmap` + `CARFIELD_CLUSTER_RUN` IOCTL + `pulp_hello.c` yazıldı, derlendi
- **Aşama 3** ⏳ SIRADA — EOC polling → interrupt, mailbox protokolü
- **Aşama 4** ⏳ SIRADA — Python arayüzü (ctypes/cffi)

## Sıradaki Oturum Başlangıç Noktası

Aşama 3: `driver/carfield.c`'deki polling döngüsünü `wait_event_interruptible` + IRQ handler'a çevir.
Giovanni/Daniele'ye önce sor: `boot_addr` değeri ve mailbox ID.

## Repo

GitHub: https://github.com/AlidotEmre/carfield-work
Lokalinde: `~/carfield-work` — yeni cihazda `git clone` + symlink (README'de adımlar var)

---

## Önemli Kaynaklar

- Carfield repo: https://github.com/pulp-platform/carfield
- Linux kernel labs: https://linux-kernel-labs.github.io/refs/heads/master/labs/device_drivers.html
- Carfield paper: https://pulp-platform.org/docs/dac2023/Carfield_SSH_SoC_DAC23_v2_pdf.pdf
