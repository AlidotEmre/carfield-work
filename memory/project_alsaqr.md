---
name: project-alsaqr
description: Kullanıcının Academic Research Assistant olarak İtalya'da yapacağı Carfield SoC Linux kernel driver stajı — 22 Haziran 2026 başlangıç
metadata: 
  node_type: memory
  type: project
  originSessionId: 70dc4cca-5926-4173-bb05-28d4f9b9e6d5
---

**Staj detayı:**
- **Kurum:** PoliTo Energy Center, İtalya
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

## Mailbox Topolojisi — Daniele'nin yanıtı (2026-06, mail) — KRİTİK GÜNCELLEME

FPGA bitstream'de 3 aktif mailbox var (unit'te daha fazlası var ama interrupt hattına
bağlı değiller, kullanılmayacak). **Sadece `INT_SND_SET` aktif** — `INT_RCV_SET`'i
unut, mimari sadeleştirilmiş.

| Mbox ID | Yön | Amaç |
|---|---|---|
| 1 | Host → OpenTitan | En çok kullanılacak mbox. `INT_SND_SET` yazmak OpenTitan'ın PLIC'ine bağlı interrupt'ı tetikliyor. |
| 5 | PULP → Host/CVA6 | PULP cluster'dan host'a bildirim (konvansiyonel olarak PULP kullanıyor) |
| 7 | OpenTitan → Host/CVA6 | OpenTitan'dan host'a bildirim |

**Yapısal sonuç:** Gönderdiğin mailbox ile completion aldığın mailbox AYNI DEĞİL
(tek `struct carfield_mbox` modeli kırılıyor — outbound mbox 1 + inbound mbox 5/7
ayrı handle'lar olacak şekilde bölünmesi gerekiyor). `INT_SND_EN`'in hangi tarafta
(outbound mı inbound mı) set edileceği maille netleşmedi — **toplantıda doğrulanacak**.

**PULP'a komuta mailbox YOK** — PULP cluster'a giden yol Event Unit (EU) üzerinden
olacak, Daniele toplantıda gösterecek. Mailbox'ta PULP'a giden hiçbir şey kalmıyor,
sadece PULP→host bildirimi (mbox 5) mailbox'ta duruyor. `CARFIELD_CLUSTER_RUN`
zaten mailbox'tan bağımsızdı ama EOC sinyalleşmesinin de ileride EU'ya kayması olası.

**LETTER0/LETTER1 offset belirsizliği önemini kaybediyor:** Daniele, register
offsetleriyle (LETTER0=0x80, LETTER1=0x8C şüphesi) uğraşmamıza gerek olmadığını,
bunların low-level API'lerle birlikte sağlanacağını söyledi. → Register-access
katmanına (mevcut `mbox_reg()`/`writel`/`readl` makroları) fazla yatırım yapma,
o katman yakında değişecek; üstündeki mantık (param gönder → completion bekle →
cevabı oku) kalıcı.

**Buffer/mesaj protokolü onaylandı:** OpenTitan bir mailbox-interrupt üzerinden
"API" sunuyor şeklinde düşünülebilir — wait-for-interrupt'ta bekliyor, mbox 1'e
`INT_SND_SET` yazılınca tetikleniyor, önceden letter0/letter1'e yazılmış 2x32-bit
parametreyi okuyor. Daha fazla metadata gerekirse: bellekte custom struct tanımla,
struct adresini letter'lardan birine koy — header+map page yaklaşımımız (mevcut
`carfield_paging.c`) bu modelle birebir uyumlu, **etkilenmiyor**. Layout'u biz
tanımlıyoruz, OpenTitan firmware'i parse edecek.

**Adres genişliği:** 32-bit'in yeterli olduğu varsayımı Daniele tarafından makul
bulundu ("küçük bellekler" gerekçesiyle) ama kesin onay Luca/Francesco'dan
bekleniyor — **henüz kesin değil**.

**PULP cluster boot adresi:** 0x78000000 varsayımı DOĞRULANDI (Daniele "you can
assume" dedi) — canlı olarak toplantıda tekrar kontrol edilecek.

**Toplantı:** Daniele önümüzdeki hafta (Pazartesi veya Cuma 15:00) kod üzerinden
mailbox mimarisini ve EU'yu canlı gösterecek bir görüşme öneriyor — tarih henüz
teyitli değil.

### Kod tabanına etkisi (henüz uygulanmadı — bkz. Sıradaki Oturum)
1. **Yapısal:** `carfield_mbox.c`'deki tek `struct carfield_mbox` outbound (mbox 1)
   + inbound (mbox 5, mbox 7) olarak ayrılmalı; `INT_SND_EN` ataması buna göre
   taşınmalı (toplantıda netleşecek).
2. **Mekanik ama anlam değişiyor:** Doorbell artık `INT_RCV_SET` değil
   `INT_SND_SET` (mbox 1) — kodda "completion" register'ı olarak kullanılan
   `INT_SND_*` artık "doorbell" rolünde.
3. **IRQ handler:** Host iki kaynaktan (mbox 5, mbox 7) interrupt alacak —
   handler hangi mailbox'ın tetiklendiğini `STAT` okuyarak demux etmeli veya
   kaynak başına ayrı handler kaydedilmeli.
4. **Yeni bileşen:** PULP yolu için Event Unit (EU) öğrenilip yazılmalı —
   mailbox katmanından tamamen bağımsız, henüz başlanmadı.
5. **`mock_mmio` simülasyonu rework gerekiyor:** Tek-mailbox echo modeli yeni
   topolojiye uymuyor — "PULP thread" aslında OpenTitan modeli olmalı (mbox 1
   inbound, mbox 7 outbound) + mbox 5 ayrı yol; doorbell `INT_SND_SET`'e
   dönmeli. Shim/mock-MMIO altyapısı yeniden kullanılabilir, sadece topoloji
   değişiyor.
6. **Değişmeyenler:** Interrupt-driven completion mantığı (`wait_event`,
   EOC polling değil), paging zinciri (pin → `page_to_phys` → header/map →
   unpin), level-sensitive `CLR` disiplini, doorbell öncesi `wmb()`/fence —
   hepsi geçerliliğini koruyor.

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
- **Aşama 3** ⏳ DEVAM EDİYOR — alt maddelere bölündü, durumlar aşağıda
  - ✅ Mailbox protokolü (send/doorbell/completion/IRQ) yazıldı ve hardware-free
    simülasyonla uçtan uca test edildi — bkz. ayrı repo
    `mailbox-simulation-withoutFPGA` (`carfield_mbox.c/.h` + mock MMIO + simüle
    PULP thread'i, gerçek FPGA/donanım kullanmadan)
  - 🟡 Paging zinciri (header page + map page, pin/unpin) yazıldı — titanssl
    referansından ported, 3 düzeltmeyle (`copy_from_user`/`copy_to_user` dönüş
    kontrolü, `pin_user_pages_fast`/`unpin_user_pages`, `GFP_DMA32` ile 32-bit
    adres kırpma riskinin önlenmesi). Sadece sayfa-düzeni matematiği
    (`carfield_paging_math.c`) gerçekten test edildi (6/6 PASS, düz gcc).
    Gerçek `pin_user_pages` yolu **henüz derlenmedi/test edilmedi** — WSL2'nin
    kendi kernel'i (`*-microsoft-standard-WSL2`) için `linux-headers` paketi
    yok.
  - 🟡 EOC polling → interrupt dönüşümü — `driver/carfield.c`'deki
    `CARFIELD_CLUSTER_RUN` içindeki busy-poll döngüsü `wait_event_interruptible`
    + IRQ handler'a çevrildi (commit `6843a8c`), gerçek 5.15 kernelde
    derlendi/yüklendi. `CARFIELD_EOC_IRQ` hâlâ yer tutucu (`0`) — PULP'un
    `eoc_o` sinyali host'a Cheshire'ın `intr_ext_i[0]`'ı (`pulpcl_eoc`)
    olarak ulaşıyor (doğrulandı, pulp-platform.github.io) ama kesin PLIC
    source ID'si henüz bilinmiyor. ISR/wait_event mantığının fiilen
    çalıştığı donanımsız test edilemedi (kasıtlı, kullanıcı tercihi).
  - ✅ Mailbox formatı netleşti — Daniele'nin yanıtıyla (bkz. "Mailbox
    Topolojisi — Daniele'nin yanıtı" bölümü): 2-letter format kalıyor (5-word
    titanssl ABI'si değil), ama topoloji tek-mailbox modelinden yöne-özel
    3-mailbox modeline (1: host→OpenTitan, 5: PULP→host, 7: OpenTitan→host)
    değişti. Bu nedenle "netleşmedi" maddesi kapandı ama **kod henüz bu yeni
    modele göre revize edilmedi** — mevcut `carfield_mbox.c` hâlâ eski
    tek-mailbox (send+completion aynı yerde) varsayımıyla yazılı, rework
    gerekiyor (bkz. aşağıdaki "Sıradaki Oturum").
  - ✅ `carfield_mbox.c` 3-mailbox modeline göre rework edildi
    (`mailbox-simulation-withoutFPGA` repo, commit `a28ad6d`): tek
    `struct carfield_mbox` → `carfield_mbox_out` (mbox 1, send-only) +
    `carfield_mbox_in` (mbox 5/7, wait-only) olarak ayrıldı, doorbell
    `INT_RCV_SET`'ten `INT_SND_SET`'e döndü. `opentitan_sim.c` (yeni,
    eski `pulp_sim.c`'nin gerçekte modellediği şey) mbox 1'de dinleyip
    mbox 7'de cevaplıyor; `pulp_sim.c` artık bağımsız olarak mbox 5'te
    bildirim yapıyor. WSL'de uçtan uca test edildi, PASS. `INT_SND_EN`'i
    hangi tarafın yazacağı hâlâ teyitsiz (koda yorum olarak işaretli).
  - ⏳ Mailbox kodu (`carfield_mbox.c`) ile asıl driver (`carfield.c`) henüz
    entegre değil — şu an iki paralel parça olarak duruyor (biri gerçek
    kernel/ioremap kullanıyor, diğeri mock MMIO + userspace pthread).
  - 🆕 PULP'a komuta mailbox yok, Event Unit (EU) kullanılacak — henüz
    başlanmadı, Daniele toplantıda gösterecek.
- **Aşama 4** ⏳ SIRADA — Python arayüzü (ctypes/cffi)

## Sıradaki Oturum Başlangıç Noktası

1. Daniele ile toplantı (önümüzdeki hafta Pazartesi veya Cuma 15:00, tarih
   teyitli değil) — EN/STAT sahipliğinin outbound mu inbound mu tarafta
   olduğunu doğrula (hem mailbox `INT_SND_EN` hem de EOC `CARFIELD_EOC_IRQ`
   için gerçek PLIC source ID), EU'yu canlı gör, register low-level
   API'lerini al.
2. ~~carfield_mbox.c rework~~ ✅ bitti (commit `a28ad6d`, bkz. yukarı).
3. ~~mock_mmio/pulp_sim güncellemesi~~ ✅ bitti (`opentitan_sim.c` +
   rework edilmiş `pulp_sim.c`, bkz. yukarı).
4. `carfield_mbox.c`'yi (mock MMIO + userspace pthread) `driver/carfield.c`'ye
   (gerçek kernel/ioremap) entegre et — bu, mbox 1/5/7 için de gerçek IRQ
   numaraları gerektirecek, henüz yok (madde 1'i bekliyor).
5. PULP Event Unit (EU) — henüz başlanmadı; `hal/eu/eu_v3.h` (pulp-sdk,
   cluster-side HAL) referans olabilir ama host-side entegrasyon ayrı iş.

## Repo

GitHub: https://github.com/AlidotEmre/carfield-work
Lokalinde: `~/carfield-work` — yeni cihazda `git clone` + symlink (README'de adımlar var)

---

## Önemli Kaynaklar

- Carfield repo: https://github.com/pulp-platform/carfield
- Linux kernel labs: https://linux-kernel-labs.github.io/refs/heads/master/labs/device_drivers.html
- Carfield paper: https://pulp-platform.org/docs/dac2023/Carfield_SSH_SoC_DAC23_v2_pdf.pdf
