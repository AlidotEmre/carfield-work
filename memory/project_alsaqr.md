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
   (bkz. `CLAUDE.md` madde 1, `QUESTIONS_FOR_DANIELE.md` madde 4).
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
- **Aşama 4** ⏳ SIRADA — Python arayüzü (ctypes/cffi)

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
| `fpo/fps/lps` geometrisi uçtan uca doğru işleniyor | Gerçek mailbox register semantiği (doorbell/completion kablolaması, IRQ 58 vb.) |
| Timeout, hata, release/unpin yolları canlı bir tüketici altında çalışıyor | 32-bit'in gerçek Carfield RAM'ine sığması; gerçek OT firmware davranışı/performansı |

**Rapor ederken ifade:** "ratified kontratı uygulayan bir yazılım mock'una karşı uçtan
uca doğrulandı" — asla sadece "doğrulandı" denmemeli (spec'in kendi uyarısı).

**Why:** `MOCK_OT_SPEC.md` §9 Definition of Done'ın kod/test tarafındaki tüm maddeleri
bununla kapandı; sadece bu belgeleme maddesi açıktı, şimdi o da kapandı.

**How to apply:** Mock OT'yi tekrar "test edilmedi" diye sunma. Bu, gerçek mailbox
donanımının yerini TUTMUYOR — mailbox entegrasyonu (`carfield_mbox.c` → `carfield.c`)
hâlâ yapılmadı ve Daniele toplantısından gelecek gerçek IRQ/PLIC numaraları ile
`INT_SND_EN` yön bilgisini bekliyor (bkz. `QUESTIONS_FOR_DANIELE.md` madde 1-2).

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

## Sıradaki Oturum Başlangıç Noktası

1. Daniele ile toplantı/mail — yukarıdaki 3 çelişkiyi (SND/RCV, çıktı hedefi,
   PULP mailbox var mı) ve `magic` alanı yanlış anlamasını netleştir; EOC
   `CARFIELD_EOC_IRQ` için gerçek PLIC source ID hâlâ bekleniyor. (Kullanıcı bu
   turu kendi yürütüyor.)
2. ~~carfield_mbox.c rework~~ ✅ bitti (commit `a28ad6d`, bkz. yukarı) — AMA yukarıdaki
   Çelişki 1 nedeniyle muhtemelen yanlış register kullanıyor, netleşmeden dokunma.
3. ~~mock_mmio/pulp_sim güncellemesi~~ ✅ bitti (`opentitan_sim.c` +
   rework edilmiş `pulp_sim.c`, bkz. yukarı).
4. `carfield_mbox.c`'yi (mock MMIO + userspace pthread) `driver/carfield.c`'ye
   (gerçek kernel/ioremap) entegre et — bu, mbox 1/5/7 için de gerçek IRQ
   numaraları gerektirecek, henüz yok (madde 1'i bekliyor). Ayrıca artık çıktının
   host'a değil PULP L2'ye gidebileceği modelini de hesaba katmalı.
5. PULP Event Unit (EU) — henüz başlanmadı; `hal/eu/eu_v3.h` (pulp-sdk,
   cluster-side HAL) referans olabilir ama host-side entegrasyon ayrı iş.
   `HOST_TO_CLUSTER_MBOX`/`CLUSTER_TO_HOST_MBOX` çelişkisi netleşince EU'nun hâlâ
   gerekli olup olmadığı da netleşecek.

## Repo

GitHub: https://github.com/AlidotEmre/carfield-work
Lokalinde: `~/carfield-work` — yeni cihazda `git clone` + symlink (README'de adımlar var)

---

## Önemli Kaynaklar

- Carfield repo: https://github.com/pulp-platform/carfield
- Linux kernel labs: https://linux-kernel-labs.github.io/refs/heads/master/labs/device_drivers.html
- Carfield paper: https://pulp-platform.org/docs/dac2023/Carfield_SSH_SoC_DAC23_v2_pdf.pdf
