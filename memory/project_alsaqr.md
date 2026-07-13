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

**2026-07-06 NOT:** Bu bölümdeki bazı bilgiler (özellikle "sadece `INT_SND_SET`
aktif" ve "PULP'a mailbox yok") 2026-07-06'da gelen yeni mail + gerçek `mbox.h`
koduyla ÇELİŞİYOR — bkz. aşağıdaki "Daniele'den Yeni Mail + Gerçek mbox.h Kodu"
bölümü. Bu bölümü tek başına güncel/kesin kabul etme.

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

1. **`fence.i` bir veri-belleği barrier'ı DEĞİL** — RISC-V'de sadece
   icache/self-modifying-code senkronizasyonu yapar. Önceki "cache flush
   zorunlu" ifadesi titanssl'den (muhtemelen yanlış anlaşılmış) miras
   kalmıştı, düzeltildi (bkz. TITANSSL_ANALYSIS.md §3). CVA6↔mailbox
   coherence + gerçek bir data fence/CMO gerekip gerekmediği açık soru
   (bkz. `CLAUDE.md` madde 1, `docs/QUESTIONS_FOR_TEAM.md` madde 4).
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
  - ✅ Mock OpenTitan consumer (kthread tabanlı, `MOCK_OT_SPEC.md`) yazıldı
    ve **gerçek kernelde (carfield-VM) uçtan uca doğrulandı** (2026-07-06)
    — bkz. aşağıdaki "Mock OpenTitan Consumer — Gerçek Kernel Testi" bölümü.
- **Aşama 4** ✅ İlk tur tamamlandı — Python arayüzü (`pyiface/`), carfield-VM'de
  `mock_ot=1` altında `sudo python3 -m pytest tests/test_pyiface.py -v` ile
  **10/10 PASS** (2026-07-09, kullanıcı bildirdi). Detay aşağıda "Aşama 4 — VM
  Testi Sonucu" bölümünde.

## Mock OpenTitan Consumer — Gerçek Kernel Testi (2026-07-06)

`driver/carfield_mock_ot.c/.h` (commit `e78c50b`, spec: `MOCK_OT_SPEC.md`) — kthread
tabanlı bir mock OpenTitan consumer, paging zincirinin ilk gerçek "tüketicisi". carfield-VM
üzerinde (`git pull` → HEAD `9b89aa5`) derlendi, `insmod carfield-mod.ko mock_ot=1` ile
yüklendi ve `tests/mock_ot_test` ile MOCK_OT_SPEC.md §7'nin tüm senaryoları çalıştırıldı:

- 3 tekrar × 4 case (single-page, page-straddling, large-malloc-scattered,
  mmap-aligned-fpo0) → hepsi PASS
- `mock_no_reply=1` → ioctl `-ETIMEDOUT` (~2025 ms), sayfa sızıntısı yok
- `mock_corrupt_magic=1` → `ERR_MAGIC` → `-EILSEQ`, tek §5 red senaryosu doğrulandı
- `mock_bad_xform=1` → yanlış anahtarla transform, doğru-anahtar karşılaştırması FAIL
  verdi (testin vacuous olmadığının kanıtı)
- `rmmod` sonrası `dmesg`'de Bad-page/BUG/leak uyarısı yok — sadece bilinen/zararsız
  `ioremap soc_ctrl/int_cluster failed (no hardware?)` (yükleme anında, donanımsız VM)

**§8 tablosu — bu testin neyi kanıtladığı / kanıtlamadığı** (`MOCK_OT_SPEC.md`'den,
sonuçla birlikte):

| Kanıtlanan (yazılım, gerçek kernel) | Kanıtlanmayan (sadece FPGA'da) |
|---|---|
| Header/map'teki fiziksel adresler bağımsız olarak çözülebilir ve okunabilir | OT'nin interconnect'i bu DDR adreslerine erişebilir / adres-görünümü çevirisi |
| Fiziksel adres üzerinden yazma, pinlenmiş user buffer'a ulaşıyor (`FOLL_WRITE`) | CVA6↔OT cache coherence (doorbell öncesi flush sorusu, hâlâ açık) |
| `fpo/fps/lps` geometrisi uçtan uca doğru işleniyor | Gerçek mailbox register semantiğinin silikon üzerinde fiilen çalışması (doorbell yazımı gözlemlendi mi, completion IRQ 58 gerçekten tetikleniyor mu) — register haritası ve IRQ numarası artık teyitli (`docs/QUESTIONS_FOR_TEAM.md` madde 8), sadece gerçek donanıkta henüz denenmedi |
| Timeout, hata, release/unpin yolları canlı bir tüketici altında çalışıyor | 32-bit'in gerçek Carfield RAM'ine sığması; gerçek OT firmware davranışı/performansı |

**Rapor ederken ifade:** "ratified kontratı uygulayan bir yazılım mock'una karşı uçtan
uca doğrulandı" — asla sadece "doğrulandı" denmemeli (spec'in kendi uyarısı).

**Why:** `MOCK_OT_SPEC.md` §9 Definition of Done'ın kod/test tarafındaki tüm maddeleri
bununla kapandı; sadece bu belgeleme maddesi açıktı, şimdi o da kapandı.

**How to apply:** Mock OT'yi tekrar "test edilmedi" diye sunma. Bu, gerçek mailbox
donanımının yerini TUTMUYOR — mailbox entegrasyonu (`carfield_mbox.c` → `carfield.c`)
hâlâ yapılmadı ve Daniele toplantısından gelecek gerçek IRQ/PLIC numaraları ile
`INT_SND_EN` yön bilgisini bekliyor (bkz. `docs/QUESTIONS_FOR_TEAM.md` madde 1-2).

## Daniele'den Yeni Mail + Gerçek mbox.h Kodu (2026-07-06) — ÖNEMLİ ÇELİŞKİLER, DİKKATLİ OL

Kullanıcının 3 Temmuz 2026 mailine (paging zinciri VM'de doğrulandı bilgisi + header
struct + cache coherence + mock-OT soruları) Daniele'den bir yanıt geldi, **artı**
Daniele bu sefer gerçek bir kod dosyası (`mbox.h`, muhtemelen OT ROM/firmware tarafı —
`#include "sw/device/silicon_creator/rom/string_lib.h"` yolu OpenTitan'ın kendi repo
düzenini kullanıyor) paylaştı. Bu, önceki (2026-06) mail bilgisiyle **doğrudan çelişen**
noktalar içeriyor — kod tarafında HENÜZ HİÇBİR ŞEY DEĞİŞTİRİLMEDİ, kullanıcı önce
Daniele ile netleştirecek.

**mbox.h register haritası (gerçek, somut offsetler):**
```
CAR_MBOX_BASE_ADDR = 0x40000000, her mailbox id için +(id*0x100):
  +0x00 INT_SND_STAT   +0x40 INT_RCV_STAT
  +0x04 INT_SND_SET    +0x44 INT_RCV_SET
  +0x08 INT_SND_CLR    +0x48 INT_RCV_CLR
  +0x0C INT_SND_EN     +0x4C INT_RCV_EN
  +0x80 LETTER0        +0x84 LETTER1
```
(Önceki "LETTER0=0x80, LETTER1=0x8C şüphesi" notu YANLIŞMIŞ — gerçek LETTER1=0x84.)

**ÇELİŞKİ 1 — doorbell register'ı:** mbox.h'deki AKTİF (yorumsuz) `mailbox_send()`
doorbell olarak **`INT_RCV_SET`** yazıyor (`INT_SND_SET` + `INT_SND_EN` kullanan eski
versiyon kodda yorum satırına alınmış, yazan kişi "I feel like it does not respect the
documentation" notu düşmüş). Bu, 2026-06 mailindeki "sadece `INT_SND_SET` aktif,
`INT_RCV_SET`'i unut" bilgisinin **tam tersi**. `carfield_mbox.c` reworku (commit
`a28ad6d`, bkz. yukarı) o Haziran bilgisine göre yazıldı — yani şu an muhtemelen yanlış
register'ı tetikliyor olabilir. **Netleşmeden dokunma.**

**ÇELİŞKİ 2 — çıktının gerçek hedefi, Mock OT'nin temel varsayımını kırıyor:** Yeni
mailde Daniele, OT'nin (addr_src, addr_dst, size) ile DMA yapacağını ve
`addr_dst=0x78000000` (**PULP cluster L2 base**) olacağını yazdı — yani OT girdiyi
host'un pinlenmiş buffer'ından okuyor ama **çıktıyı host'a değil doğrudan PULP L2'ye
yazıyor**. `MOCK_OT_SPEC.md`/`carfield_mock_ot.c`'nin bütün modeli (in-place XOR,
"write-back into the pinned user buffer" §8 kanıtı) bu gerçek veri akışını temsil
ETMİYOR. Mock hâlâ değerli (pin/build/release zincirini doğrulaması bakımından) ama
gerçek OT davranışının bir benzetmesi değil — bu ayrım rapor edilirken vurgulanmalı.
İyi haber: Daniele'nin tarif ettiği transfer şekli (ilk sayfa kısmi, orta sayfalar tam,
son sayfa kısmi — `fpo/fps/lps/nop` ile) bizim header geometrimizle bire bir örtüşüyor,
sadece hedef adres modeli farklı.

**ÇELİŞKİ 3 — PULP'un mailbox'ı var mı yok mu:** mbox.h'de açıkça
`HOST_TO_CLUSTER_MBOX=6` ve `CLUSTER_TO_HOST_MBOX=2` (yorumda `//15` alternatifi de
var) tanımlı — yani host↔PULP arası bir mailbox yolu görünüyor. 2026-06 mailinde ise
"PULP'a komuta mailbox YOK, Event Unit üzerinden olacak" denmişti. İki bilgi de
Daniele'den ama birbiriyle çelişiyor.

**Daniele'nin olası yanlış anlaması:** Mailinde "Index of the starting page in the
page map; (is it the magic number?)" diye soruyor — bizim header'ımızdaki `magic`
alanı sabit bir sanity-check değeri (`0xCA4F1E1D`), bir sayfa index'i DEĞİL. Netleşmezse
OT firmware'i muhtemelen yanlış alanı index sanabilir. (Kullanıcı bunu kendi
halledecek.)

**Çözülen sorular:**
- `INT_SND_EN` sahipliği (eski madde 1) → **"Currently INT_SND_EN can be written by
  all domains."** Sabit bir taraf yok, herkes yazabiliyor.
- Cache coherence (eski madde 4, `fence.i` tartışması) → somut cevap: L3'te header
  tutarsan flush şart; Daniele L2 belleği (0x78 civarı) öneriyor, OT için L3'ü hiç
  kullanmamış, struct okunduktan sonra o L2 bölgesi üzerine yazılabilir (transient).
  Bu, şu anki "keyfi host userspace sayfasını pinle" yaklaşımının (muhtemelen
  L3/cacheable RAM) revize edilmesi gerekebileceği anlamına geliyor — henüz KARAR
  YOK, sadece somut bir veri noktası.
- Mock'un gerekliliği doğrulandı: Daniele "FPGA'dan başka mock bilmiyorum" dedi.

**Why:** Bu iki kaynak (yeni mail + gerçek kod), önceki "netleşti" sayılan bazı
maddeleri (mailbox topolojisi, SND/RCV, PULP mailbox var mı) yeniden açıyor. Projenin
gidişatı bu netleşmeye göre değişebilir.

**How to apply:** Yukarıdaki 3 çelişkiyi "çözüldü" gibi sunma, hâlâ AÇIK — kullanıcı
Daniele ile kendi netleştirecek. Netleşene kadar `carfield_mbox.c`/`carfield.c`'de
SND/RCV veya buffer-model varsayımına dayanan hiçbir kod değişikliği önerme/yapma.

## Aşama 4 — VM Testi Sonucu (2026-07-09)

carfield-VM'de `sudo insmod carfield-mod.ko mock_ot=1` sonrası
`sudo python3 -m pytest tests/test_pyiface.py -v`: **10/10 PASS**, hiçbir
sorun bildirilmedi (geometri süiti 4 case × dahili 3 tekrar, demo xform
roundtrip, `mock_no_reply`/`mock_corrupt_magic`/`mock_bad_xform` fault
path'leri, size==0/oversize sınır case'leri — hepsi geçti).
`PYIFACE_SPEC.md` §8 DoD'nin ilk maddesi ("Suite green 3× against
mock_ot=1") kapandı — `test_geometry_suite_three_times` testin kendisi
3 tekrarı içeriyor, tek yeşil çalıştırma yeterli.

**Why:** Bu, Aşama 4'ün gerçek kernelde doğrulandığının somut kanıtı —
önceki oturumda sadece elle/`py_compile` ile doğrulanabilmişti.

**How to apply:** Aşama 4'ü tekrar "doğrulanmadı" diye sunma, ilk tur
kapandı. Sıradaki iş gerçek op'lar (load_model/run_inference) veya
mailbox netleşmesi.

## Daniele'nin Yeni Cevapları (2026-07-09) — 3 çelişkinin durumu + yeni bulgular

Kullanıcı Daniele ile devam eden mail zincirinin en son halkasını paylaştı
(kendi Temmuz 6 15:21 mailine Daniele'nin satır arası cevapları + Torino'ya
20 Haziran'dan itibaren bir hafta gelme teklifi). Kod tabanı bu cevaplara
karşı elle taranarak çapraz kontrol edildi (bkz. aşağıdaki "Why/How").

**Çelişki 1 (doorbell register) — ÇÖZÜLDÜ, mevcut kod zaten DOĞRU:**
Daniele üç yön için de doğruladı: host→OT (mbox1) / OT→host (mbox7) /
PULP→host (mbox5) hepsi `INT_SND_SET`. `mbox.h`'deki `mailbox_send()`'in
sonunda `INT_RCV_SET`'e yazması **kasıtsız/zararsız ölü kod** — o
register'a bağlı hiçbir interrupt hattı yok; Daniele "mbox.h'deki
primitifleri şimdilik black-box olarak al" dedi. `carfield_mbox.c`
reworku (`a28ad6d`, `mailbox-simulation-withoutFPGA` / `carfield_mbox_sim`)
zaten `INT_SND_SET` kullanıyor — **kod değişikliği gerekmiyor.**

**Çelişki 3 (PULP mailbox var mı) — ÇÖZÜLDÜ, yok:** Daniele
`HOST_TO_CLUSTER_MBOX=6`/`CLUSTER_MBOX_EVT=22`'nin "hiç kullanılmadığını,
yanıltıcı olduğunu, silinebileceğini" söyledi. Haziran bilgisi (PULP'a
mailbox yok, EU üzerinden) doğrulandı. Kod tabanımızda zaten bu macrolara
hiç referans yok — etkilenen kod yok, sadece EU-only varsayımı kesinleşti.

**Çelişki 2 (OT çıktı hedefi) — kısmen netleşti, ASIL SORU (sonuç host'a
nasıl döner) hâlâ açık, Daniele bilinçli olarak erteledi:** L2 boyutu
1 MiB (`0x78000000`–`0x78100000`), sahiplik kavramı yok (paylaşımlı SoC
belleği, cluster executable + ara hesap verisi burada), OT struct'ı
okuduktan sonra o L2 bölümü üzerine yazılabilir. Map page L2'de olmak
zorunda değil — OT'nin DMA'sı tüm belleklere master erişimli, host DRAM'de
kalabilir (sadece header için L2 önerildi, cache flush sorununu
atlatmak için). **Sonucun host/Python tarafına nasıl döneceği** sorusu
Daniele tarafından açıkça ertelendi: "execution flow üzerine biraz daha
düşünmemiz lazım, ilerledikçe konuşalım." Mock OT'nin host-buffer'a
geri yazma modelinin gerçek donanımı temsil etmediği (zaten
`carfield_mock_ot.c`/`demo.py` docstring'lerinde disclaimer'lı) bir kez
daha doğrulanmış oldu — **değiştirilecek bir şey yok, sadece teyit.**

**`MBOX_LETTER1` bug — DÜZELTİLDİ (2026-07-09, bu dosyaya geç işlendi 2026-07-13):**
`carfield_mbox.h` (`carfield_mbox_sim` reposu) satır 46'daki
`#define MBOX_LETTER1 0x8C` (yanlış, üstünde `"!!! VERIFY..."` yorumu
vardı) gerçek `mbox.h`'den doğrulanan değere (**0x84**) düzeltildi.
`carfield_mbox_sim` commit `e5708e6` ("fix: correct MBOX_LETTER1 offset
from guessed 0x8C to confirmed 0x84"), `origin/main`'e push edildi,
`./test_mbox` PASS. Bu satır önceden "henüz uygulanmadı, onay bekleniyor"
diyordu — bu, [[feedback-memory-sync]]'in tarif ettiği türden bir
belgeleme gecikmesiydi (fix ayrı bir repoda yapıldığı için bu dosyaya
hiç yansımamıştı), global hafıza taramasında (2026-07-13) fark edilip
buraya işlendi.

**YENİ BELİRSİZLİK — L2 bölge sayısı çelişkisi:** `driver/carfield.c`
mmap tablosu 4 ayrı 1 MiB L2 bölgesi tanımlıyor
(`L2_INTL_0=0x78000000, L2_CONT_0=0x78100000, L2_INTL_1=0x78200000,
L2_CONT_1=0x78300000`, toplam 4 MiB), Daniele ise "L2 boyutu 1 MiB,
`0x78000000`–`0x78100000`" dedi (tek bölge gibi). INTL/CONT muhtemelen
aynı fiziksel SRAM'in iki görünümü (interleaved vs contiguous adres
alanı) ya da INTL_1/CONT_1 ikinci bir cluster'a ait olabilir — netleşmedi.
**Toplantıda/mailde sorulmalı**, `docs/QUESTIONS_FOR_TEAM.md`'ye eklendi.

**Diğer küçük teyitler:** `magic` alanının sayfa index'i olmadığı,
sabit sanity değeri olduğu ve başlangıç sayfasının `map[0]` olduğu
netleşti (kod zaten bu şekilde yazılmıştı, değişiklik yok — sadece
Daniele'nin yanlış anlaması düzeltildi). Transfer tavanı: FPGA'da fiziksel
limit 1 MiB, bizim map-page kapasitemiz (4 MB, `PAGE_SIZE/sizeof(u32)`
sınırı) zaten üstünde, sorun değil. OT'nin PLIC'i tek kaynak (IRQ 159)
kullanıyor, `letter1` değeriyle demux ediyor — bu OT'nin kendi tarafı,
bizim `CARFIELD_EOC_IRQ` sorumuzu (madde 2, hâlâ açık) etkilemiyor.
Daniele Pazartesi 20 Temmuz'dan itibaren bir hafta Torino'da, yüz yüze
görüşme opsiyonu sundu.

**Why:** Bu mail zinciri, önceki oturumda "açık" işaretlenen 3
çelişkiden ikisini kapattı ve üçüncüsünü (sonuç dönüş yolu) kasıtlı
olarak sonraya bıraktı; ayrıca kod taraması sırasında hafızaya
işlenmiş ama koda hiç yansımamış bir gerçek bug (LETTER1) ortaya çıktı.

**How to apply:** Çelişki 1 ve 3'ü tekrar "açık" diye sunma, kapandı.
Çelişki 2'nin (sonuç dönüş yolu) AÇIK kaldığını unutma — Daniele kendi
isteğiyle erteledi, zorlama. `MBOX_LETTER1` düzeltmesi ve L2 bölge
sayısı sorusu bir sonraki oturumun ilk maddeleri olmalı (aşağıdaki
"Sıradaki Oturum" güncellendi).

## Sıradaki Oturum Başlangıç Noktası (2026-07-09 itibarıyla güncellendi)

1. ~~`MBOX_LETTER1` bugfix~~ ✅ bitti (`carfield_mbox_sim` commit `e5708e6`,
   `0x8C` → `0x84`, push edildi, test PASS) — bkz. yukarı.
2. ~~carfield_mbox.c rework~~ ✅ bitti (commit `a28ad6d`) — Çelişki 1
   ÇÖZÜLDÜ, `INT_SND_SET` kullanımı DOĞRU teyit edildi, dokunmaya gerek yok.
3. ~~mock_mmio/pulp_sim güncellemesi~~ ✅ bitti (`opentitan_sim.c` +
   rework edilmiş `pulp_sim.c`, bkz. yukarı).
4. ~~`carfield_mbox.c`'yi `driver/carfield.c`'ye entegre et~~ ✅ kod tarafı
   bitti (2026-07-13, bkz. yukarı "Mailbox Hardware Backend Entegrasyonu") —
   `driver/carfield_mbox_hw.c/.h`, `real_mbox=1`. Kalan engel artık sadece
   gerçek mailbox completion IRQ'un PLIC source ID'si (bkz.
   `docs/QUESTIONS_FOR_TEAM.md` madde 8) — o gelene kadar send() çalışır,
   receive işlevsiz. Sıradaki ilk iş: carfield-VM'de derleme + spec §5
   test adımları (henüz hiç çalıştırılmadı).
5. **L2 bölge sayısı sorusu** — `driver/carfield.c`'deki 4× 1 MiB L2
   bölgesi (`INTL_0/CONT_0/INTL_1/CONT_1`) ile Daniele'nin "L2 = 1 MiB"
   cevabı arasındaki belirsizlik netleşmeli (bkz. yukarı, `docs/QUESTIONS_FOR_TEAM.md`'ye eklendi).
6. **Sonuç dönüş yolu (host mu, L2'de mi kalıyor)** — Daniele bilinçli
   erteledi, zorlamaya gerek yok, ama gerçek op tasarımından önce netleşmesi
   şart.
7. PULP Event Unit (EU) — henüz başlanmadı; `hal/eu/eu_v3.h` (pulp-sdk,
   cluster-side HAL) referans olabilir ama host-side entegrasyon ayrı iş.
   Artık kesin: mbox 6/EVT 22 yok, EU tek yol — Daniele'nin canlı
   gösterimini bekliyor.

## Mailbox Hardware Backend Entegrasyonu (2026-07-13) — kod tarafı bitti, FPGA'da hiç test edilmedi

Bir spec dosyasından (`Mailbox Integration Spec`) hareketle `carfield_mbox_sim`
reposundaki register-access kodu (`carfield_mbox.c`, zaten kernel-API
şeklinde yazılmış, userspace mock MMIO'ya karşı doğrulanmış) `driver/`e
gerçek donanım backend'i olarak taşındı: `driver/carfield_mbox_hw.c/.h`,
`carfield_mock_ot.c`'nin tanımladığı aynı 3-fonksiyonlu seam'i (send/
wait_completion/read_reply) implemente ediyor. `real_mbox=1` module param'ı
bu backend'i, `mock_ot=1` mock'u seçiyor — `carfield_init()`'te ikisi birden
set edilirse ikisi de başlatılmıyor (log + no-op, insmod'un geri kalanı
etkilenmiyor).

**IRQ 58 — ilk turda flag edildi, sonra kaynağı bulunup ÇÖZÜLDÜ (aynı
oturum):** Spec "Host mailbox IRQ (PLIC line) = 58"i "confirmed fact" diye
sunuyordu ama ilk kontrolde bu sayı proje hafızasında/mail zincirinde hiç
geçmiyordu, sadece `MOCK_OT_SPEC.md` §8'de örnek/dolgu metin olarak vardı —
`CARFIELD_MBOX_IRQ` bu yüzden önce `0` yer tutucu bırakıldı. Kullanıcı
gerçek kaynağı sağladı: `HOST_MBOX_IRQ 58`, Daniele'nin `car_lib_mbox.h`
dosyasında tanımlı, `HOST_TO_CLUSTER_MBOX`/`CLUSTER_MBOX_EVT` (ikisi de
ayrıca çürütülmüştü) ile aynı blokta ama kendisi hiç çürütülmedi. Kabul
edildi — `CARFIELD_MBOX_IRQ` artık `58`, `request_irq()` gerçek FPGA'da
canlı çalışacak (bkz. `docs/QUESTIONS_FOR_TEAM.md` madde 8). Dosyadaki
`mailbox_send()`'in `INT_RCV_SET` kullanması gibi diğer eski/düzeltme-öncesi
parçalar zaten önceki oturumda çözülmüştü, tekrar gündeme getirilmedi.

**Kod incelemesi sırasında bulunup düzeltilen 2 gerçek bug (kimse söylemeden
fark edilmezdi, bilinçli olarak arandı):**
1. `carfield_mbox_hw_send()` doorbell'dan önce `mbox_in_ot.completed`'ı
   sıfırlamıyordu — terk edilmiş bir önceki isteğe (timeout sonrası) geç
   gelen bir cevap `completed=1` ve bayat letter0/letter1 bırakabilir,
   SONRAKİ `send()`'in `wait_completion()`'ı o bayat cevabı kendi isteğinin
   cevabıymış gibi anında döndürürdü. `carfield_mock_ot_send()`'in zaten
   yaptığı "doorbell'dan önce sıfırla" disiplini eklendi.
2. `INT_SND_EN` yazımı, ilgili `request_irq()` başarılı olmadan ÖNCE
   yapılıyordu — yani hiç handler kayıtlı değilken donanımda kesmeyi
   aktif bırakıyordu. `request_irq()` başarısından SONRAYA taşındı; bu sıra
   artık gerçekten önemli, çünkü `CARFIELD_MBOX_IRQ=58` ile bu kod yolu
   FPGA'da fiilen çalışacak (aşağıya bkz).

**Bilinçli tasarım kararı (spec'in birebir port istediğinden farklı,
gerekçeli):** `carfield_mbox_sim`'in orijinal `carfield_mbox_in_irq()`'ı
harfleri ISR'da OKUMUYOR, sadece ack edip uyandırıyor; asıl okuma daha sonra
`wait`'ten dönen process context'te. Bu port ISR içinde CLR'dan hemen sonra
harfleri okuyor — gerekçe: ikinci bir mesajın harfleri "biz okumadan"
üzerine yazma riskini (level-sensitive hat + hızlı ardışık mesaj senaryosu)
minimize ediyor, orijinal tasarımdan daha güvenli bir seçim. Bu bilinçli bir
sapma, hata değil — ama Daniele'nin RTL'i "CLR yazıldıktan hemen sonra
LETTER'lar okunabilir kalır mı" konusunda teyit vermeden %100 emin
olunamaz, FPGA seansında not edilmeli.

**Diğer flag'ler (koda işlendi, yorum olarak):**
- Ioctl `CARFIELD_MOCK_OT_XFORM`/`struct carfield_mock_ot_req` ismi artık
  hem mock hem gerçek donanım trafiği için kullanılıyor — spec'in kendi
  isteğiydi ("mevcut seam'e dokunmadan entegre et"), ama isim kokusu var,
  ileride yeniden adlandırma düşünülmeli.
- Gerçek OpenTitan'ın `letter1` status kod semantiği BİLİNMİYOR —
  `MOCK_OT_SPEC.md` §5'in OK/ERR_MAGIC/... tablosu mock'un kendi icadı,
  gerçek firmware'in aynı kodları kullanacağı teyit edilmedi. Hw yolunda
  `carfield_mock_ot_status_to_errno()` ÇAĞRILMIYOR — ioctl sadece "cevap
  geldi" (0) döndürüyor, ham `letter1` `mock_status` alanında kullanıcıya
  aktarılıyor, yorumlama userspace'e bırakıldı.
- `CARFIELD_MBOX_UNIT_SIZE` (0x800, id 0-7'yi kapsıyor) donanımın gerçek
  toplam adres alanı büyüklüğünün TEYİTLİ bir değeri değil, sadece
  kullandığımız id aralığını kapsayan minimal bir pencere.

**Test durumu (2026-07-13, carfield-VM'de kullanıcı tarafından çalıştırıldı):**
- ✅ Build: `carfield_mbox_hw.o` gerçek 5.15.0-185-generic kernelinde İLK
  kez derlendi, hatasız (`carfield-mod.ko` başarıyla üretildi).
- ✅ Register-math testi (`tests/mbox_reg_test.c`) — PASS (hem MinGW/Windows
  hem carfield-VM'de gerçek gcc ile, ikisi de aynı sonucu verdi).
- ✅ `mock_ot_test` regresyonu (spec §5.1) — PASS: 3×4 case + 3
  fault-injection case, hepsi geçti. İlk çalıştırmada 3 case (timeout/
  rejection/bad_xform) `-ERANGE` ile FAIL vermişti (`carfield_paging.c:121-124`,
  `data page ... is above the 32-bit mailbox range`) — bu kontrol tonight'ın
  DEĞİL, çok önceki bir oturumun (`918ef08`, bug #2) kodu; stack buffer'ının
  kernel tarafından rastgele 4GB üstü fiziksel sayfaya denk gelmesiyle
  tetiklendi. Aynı kod (`efcaa75`), yeniden derlenip tekrar çalıştırılınca
  12/12 + 3 fault-injection hepsi PASS verdi — deterministik bir regresyon
  DEĞİL, geçici sayfa-tahsisi rastlantısı olduğu ampirik olarak doğrulandı.
- ✅ `dmesg` temiz — yüklemede/kaldırmada (`carfield: removed`) Bad-page/BUG/
  leak uyarısı yok. Mailbox ioremap'iyle ilgili hiçbir satır çıkmadı
  (`real_mbox=0` olduğu için `carfield_mbox_hw_start()` doğru no-op).
- ✅ `insmod real_mbox=1` yükleme testi (spec §5.3) — PASS. İlginç bulgu:
  bu VM'de `soc_ctrl`'in aksine `CARFIELD_MBOX_BASE_ADDR=0x40000000`'ın
  `ioremap`'i BAŞARILI oldu (`carfield_mbox_hw: real mailbox backend
  started (base=0x40000000)`), yani `request_irq(58, ...)` gerçekten
  çağrıldı — ve `-22` (`-EINVAL`) ile güvenli/non-fatal şekilde
  başarısız oldu (bu jenerik x86 VM'de gerçek IRQ 58 kaynağı yok, bu
  soc_ctrl/int_cluster ioremap hatalarıyla aynı sınıfta beklenen bir
  "donanım yok" durumu). Crash/oops/hang YOK. `INT_SND_EN` hiç
  yazılmadı (`request_irq` başarısız olduğu için) — bu, oturumda kod
  incelemesinde bulunup düzeltilen "SND_EN sadece request_irq
  başarılıysa yazılsın" sıralamasının gerçek bir MMIO adresine karşı
  ilk somut doğrulaması. `rmmod` sonrası temiz, leak yok.
- ✅ `mock_ot=1 real_mbox=1` guard testi (spec §5.4) — PASS. Tek log satırı
  (`mock_ot=1 and real_mbox=1 both set -- mutually exclusive backends,
  starting neither`), ne mock kthread'i ne mailbox ioremap'i hiç
  denenmedi (mutual-exclusion kontrolü `start()` çağrılarından önce
  çalışıyor). `rmmod` sonrası temiz.

**Sonuç: spec'in test planındaki (§5) tüm maddeler artık PASS.** DoD'nin
geri kalanı (register map, mutual exclusion, unknowns parametrize, README)
zaten önceki turda tamamlanmıştı. Bu artık "yarın FPGA'da register yazımı
dene" aşamasına hazır.

**Değiştirilen/eklenen dosyalar:** `driver/carfield_mbox_hw.c/.h` (yeni),
`driver/carfield_mock_ot.h/.c` (+`carfield_mock_ot_requested()`),
`driver/carfield.c` (ioctl backend seçimi + init/exit), `driver/Makefile`
(+`carfield_mbox_hw.o`), `tests/mbox_reg_test.c` (yeni) + `tests/Makefile`,
`docs/QUESTIONS_FOR_TEAM.md` (madde 8 eklendi, LETTER1 satırı düzeltildi),
`docs/MOCK_OT_SPEC.md` (§8 IRQ 58 netleştirildi), `carfield_mbox_sim/README.md`
(yeni, "superseded/regression testbed" notu).

**Why:** Kullanıcı yarın Daniele ile FPGA seansına "derleniyor mu" değil
"register yazımı çalışıyor mu" sorusuyla başlamak istiyor; bu gecelik iş
register haritasını/doorbell mantığını gerçek kernel koduna taşıdı. IRQ 58
kaynağı bulunduktan sonra hem SEND (doorbell) hem RECEIVE (completion IRQ)
yolu kod seviyesinde tam — ikisi de gerçek FPGA'da henüz hiç çalıştırılmadı.

**How to apply:** Sıradaki oturumda önce carfield-VM'de derleme + spec §5'in
3 test adımını (regresyon, insmod real_mbox=1, guard testi) çalıştır. Spec
DoD'sinin geri kalanı (register map, mutual exclusion, unknowns'ın
parametrize edilmesi, README güncellemesi) bu oturumda tamamlandı. IRQ 58
`docs/QUESTIONS_FOR_TEAM.md` madde 8'de kodda kullanılıyor olarak işaretli
ama **kullanıcı 2026-07-13'te Daniele ile FPGA toplantısında bunun canlı
teyit edilmesini istedi** — `car_lib_mbox.h`'deki "çürütülmedi" kaynağı
`INT_SND_SET` doorbell'ı gibi doğrudan bir Daniele teyidi değil. Toplantı
sonrası Daniele açıkça onaylarsa/reddederse buraya ve
`QUESTIONS_FOR_TEAM.md` madde 8'e işlensin.

## Repo

GitHub: https://github.com/AlidotEmre/carfield-work
Lokalinde: `~/carfield-work` — yeni cihazda `git clone` + symlink (README'de adımlar var)

---

## Önemli Kaynaklar

- Carfield repo: https://github.com/pulp-platform/carfield
- Linux kernel labs: https://linux-kernel-labs.github.io/refs/heads/master/labs/device_drivers.html
- Carfield paper: https://pulp-platform.org/docs/dac2023/Carfield_SSH_SoC_DAC23_v2_pdf.pdf
