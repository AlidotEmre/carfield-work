# Sorulacaklar — Ekip (Daniele üzerinden)

Toplantı: önümüzdeki hafta Pazartesi veya Cuma 15:00 (tarih henüz teyitli değil).
**Alternatif (2026-07-09):** Daniele Pazartesi 20 Temmuz'dan itibaren bir hafta
Torino'da olacağını, isterse Energy Center'da yüz yüze görüşebileceklerini
söyledi.
Aşağıdakiler kod tarafında yer tutucu/varsayım olarak bırakılmış, gerçek
donanım/RTL bilgisi olmadan ilerlenemeyen noktalar. Her madde hangi kod
konumunu etkilediğiyle birlikte.

## 1. Mailbox: `INT_SND_EN`'i kim yazıyor? — DÜŞÜK ÖNCELİK, pratikte çözüldü (2026-07-09)

Doorbell'ın `INT_SND_SET` olduğu netleşti (üç yön için de teyitli — host→OT,
OT→host, PULP→host). Daniele daha önce "Currently INT_SND_EN can be written
by all domains" demişti (sabit sahiplik yok). Bu, "kim yazmalı" sorusunu tam
yanıtlamasa da (kim YAPABİLİR ≠ kim YAPAR), pratik sonucu şu: host'un kendi
inbound hattı için kendi `INT_SND_EN`'ini yazması (mevcut varsayım,
`carfield_mbox_in_init()`) kimseyle çakışmaz, çünkü herkes yazabiliyor.
Zorlayıcı bir engel değil artık — toplantıda teyit edilirse iyi olur ama
kod tarafında bekleyen bir şey yok.

- Etkilenen kod: `carfield_mbox_in_init()` —
  `mailbox-simulation-withoutFPGA/carfield_mbox.c` (Windows review:
  `carfield_mbox_sim`). Mevcut varsayım (receiver kendi `INT_SND_EN`'ini
  yazıyor) güvenli, değişiklik gerekmiyor.

## 2. EOC kesmesinin kesin PLIC source ID'si — kısmen ARTIK HOST'UN SORUNU DEĞİL, ama tam netleşmedi

PULP cluster'ın `eoc_o` sinyalinin host'a `intr_ext_i[0]` (`pulpcl_eoc`,
level-sensitive) olarak ulaştığı Carfield/Cheshire mimari dokümanlarından
doğrulandı, ama bunun PLIC üzerindeki **kesin sayısal source ID'si**
(Cheshire internal+external kesme birleştirme sırasına bağlı) public
dokümanlarda yok.

- Etkilenen kod: `driver/carfield.c` — `CARFIELD_EOC_IRQ` şu an `0`
  (bilinçli yer tutucu, `request_irq()` bu değer için hiç çağrılmıyor).

**GÜNCELLEME (2026-07-30, Daniele code review):** Cluster artık host'un
doğrudan boot ettiği bir şey değil — OT boot ediyor, host için black box
(bkz. madde 9, aşağıda). Bu yüzden `CARFIELD_CLUSTER_RUN` artık bu IRQ'yu
BEKLEMİYOR (register-poke + EOC-wait modeli, host<->OT mailbox notify
modeline geçti, bkz. `project_alsaqr.md`). Ama bu IRQ hattının kendisi
(cluster'ın donanımsal `eoc_o` sinyali) hâlâ var olabilir ve hâlâ bir işe
yarayabilir — sadece artık hangi amaca hizmet ettiği belirsiz. **Yeni soru
(madde 9 ile birlikte sorulmalı):** Bu donanım IRQ'su hâlâ host'a mı
geliyor, yoksa artık OT mu araya giriyor (OT kendi mbox7 cevabıyla
"cluster bitti" mi diyor, yoksa bu ayrı, host'un hâlâ dinlemesi gereken
bağımsız bir sinyal mi)? Netleşmeden `CARFIELD_EOC_IRQ`'yu ne aktif
kullanmaya ne de tamamen ölü saymaya karar verilmiyor.

## 3. PULP Event Unit (EU) — canlı gösterim + register API — ARTIK HOST'UN SORUNU DEĞİL Kİ

PULP'a komuta giden bir mailbox yok; tüm komut/senkronizasyon yolu Event
Unit üzerinden olacaktı diye düşünülüyordu.

**GÜNCELLEME (2026-07-30, Daniele code review):** Daniele'nin cevabı net:
"OT boots the cluster, out of scope — host<->OT etkileşimini geliştiriyorsun,
OT'nin sorumluluğundaki her şeyi black box say." Yani PULP'a host'tan
doğrudan bir tetikleme (EU dahil) YOK — host sadece OT'yi notify ediyor,
OT'nin cluster'ı nasıl (EU üzerinden mi, başka bir yoldan mı) boot ettiği
tamamen OT/PULP tarafının işi, host driver'ın ilgi alanı dışında. **Bu
maddenin kendisi artık host driver'ı ilgilendirmiyor, kapatılabilir** —
host-side EU entegrasyonu diye bir şey planlanmıyor.

## 4. Cache coherence — CVA6 ↔ PULP/OpenTitan

Header/map/data sayfaları CVA6'nın cache'i üzerinden yazılıyor;
OpenTitan/PULP bunları doğrudan RAM'den okuyacak. İnterconnect coherent
değilse doorbell'dan önce gerçek bir veri-belleği fence'i/cache-management
operasyonu gerekiyor.

**Güncelleme (TITANSSL_ANALYSIS.md §3 incelemesinden):** CLAUDE.md'nin eski
"her zaman `fence.i`" kuralı düzeltildi — `fence.i` RISC-V'de yalnızca
instruction-cache/self-modifying-code senkronizasyonu yapar, veri belleği
barrier'ı DEĞİLDİR. titanssl referansı (`driver.c:379`) bunu "Clean cache!"
yorumuyla doorbell'dan önce çağırıyor ama bu muhtemelen bir yanlış anlamaydı,
bağımsız doğrulanmış bir gereksinim değil. Yani soru hâlâ açık, sadece daha
keskin: CVA6↔mailbox yolu gerçekten coherent mi (o zaman hiçbir barrier
gerekmiyor), yoksa gerçekten bir veri fence'i/CMO mu gerekiyor (o zaman
hangisi, hangi bellek bölgeleri için)?

- Bu, "simülasyonda/VM'de PASS ama gerçek FPGA'da çöp okunuyor"
  senaryolarının klasik nedeni — netleşmeden kodda spekülatif flush
  eklemek istemiyoruz.

## 5. Mailbox adres genişliği (32-bit) — Luca/Francesco onayı

Daniele 32-bit'in yeterli olduğunu makul buldu ("küçük bellekler"
gerekçesiyle) ama kesin onay Luca/Francesco'dan bekleniyor. Etkilenen
kod zaten 32-bit varsayımıyla yazıldı (`GFP_DMA32`, `u32 header_phys`
vb.) — sadece daha büyük bir adres alanı gerekirse geniş bir rework
olur, şimdiden haber vermek için sorulmalı.

## 6. PULP cluster boot adresi (0x78000000) — canlı teyit

Daniele "you can assume" demişti, DOĞRULANMIŞ sayılıyor ama toplantıda
canlı olarak tekrar kontrol edilecek (Daniele'nin kendi önerisi — artık
kodda tam boot adresini gösterebileceğini söyledi).

**Kısmen netleşti, YENİ belirsizlik (2026-07-09):** Daniele "L2 boyutu
1 MiB, `0x78000000`–`0x78100000`" dedi (sahiplik kavramı yok, paylaşımlı).
Ama `driver/carfield.c`'deki mmap tablosu 4 ayrı 1 MiB L2 bölgesi
tanımlıyor: `L2_INTL_0=0x78000000`, `L2_CONT_0=0x78100000`,
`L2_INTL_1=0x78200000`, `L2_CONT_1=0x78300000` (toplam 4 MiB). **Sorulacak:**
INTL/CONT aynı fiziksel SRAM'in iki adres-alanı görünümü mü (interleaved vs
contiguous, aynı byte'lar) yoksa INTL_1/CONT_1 farklı bir cluster'a (Spatz?)
mı ait — yoksa Daniele'nin "1 MiB" cevabı sadece `L2_INTL_0`'ı mı kastediyor?

## 7. Tek in-place buffer mi, yoksa titanssl'deki gibi ayrı input/output/meta mı?

Mock OpenTitan (bkz. `MOCK_OT_SPEC.md`) şimdilik tek bir buffer'ı yerinde
(in-place) XOR'luyor — gönderilen adres hem giriş hem çıkış. titanssl
(`titanssl_driver/titanssl.h:82-104`) bunun yerine üç ayrı buffer taşıyor:
input/output/meta, her biri kendi `dsz/nop/fpo/fps/lps/map` geometrisiyle —
çünkü input ve output BOYUTLARI farklı olabiliyor (ör. padding'li
encrypt/decrypt).

- Python akışımız da encrypted bir blob gönderip **farklı boyutta** bir
  sonuç bekleyecek gibi görünüyor (bkz. TITANSSL_ANALYSIS.md §5
  RECOMMENDATIONS, "RESERVE" maddesi) — bu doğruysa in-place tek buffer
  modeli gerçek kriptografik komutlarda yeterli olmayacak.
- Soru: gerçek OpenTitan komutları (decrypt/encrypt/key işlemleri) hep
  boyut-koruyan mı olacak, yoksa titanssl'deki gibi input≠output boyutu
  olan komutlar da olacak mı? İkincisiyse, mailbox'ın 2-letter formatını
  (tek `header_phys`) mi genişletmemiz gerekiyor, yoksa titanssl'in
  input/output/meta üçlü-header modelini mi benimsemeliyiz?

**Daniele'nin cevabı (2026-07-09):** Bilinçli olarak erteledi — "execution
flow üzerine biraz daha düşünmemiz lazım, ilerledikçe konuşuruz." Sonucun
host'a mı döneceği yoksa L2'de cluster için mi kalacağı sorusuyla birebir
bağlantılı, ikisi birlikte netleşecek.

---

## 8. Mailbox completion IRQ'unun PLIC source ID'si — 58, TOPLANTIDA CANLI TEYİT İSTENİYOR

`HOST_MBOX_IRQ 58`, Daniele'nin gerçek `car_lib_mbox.h` dosyasında tanımlı
— `CARFIELD_MBOX_IRQ` kodda şimdiden `58` olarak kullanılıyor, `request_irq()`
gerçek FPGA'da bu numarayla canlı çalışacak.

**Ama bu kaynak "çürütülmedi" seviyesinde kabul edildi, Daniele'nin
`INT_SND_SET` doorbell'ı için verdiği gibi doğrudan/pozitif bir teyit
değil.** Toplantıda Daniele'den açıkça doğrulanmalı:
- `HOST_MBOX_IRQ 58` gerçekten host'un mailbox 5/7 completion'ları için
  kullanacağı PLIC source ID mi?
- Aynı dosyadaki `HOST_TO_CLUSTER_MBOX`/`CLUSTER_MBOX_EVT` gibi eski/dead
  bir tanım değil, hâlâ geçerli mi?

- Etkilenen kod: `driver/carfield_mbox_hw.c` — `CARFIELD_MBOX_IRQ = 58`.

## 9. Cluster completion sinyali: mbox7 mi, ayrı EOC IRQ mı, ikisi de mi? — YENİ (2026-07-30)

Daniele ile yapılan online code review'da netleşti: **cluster'ı host
doğrudan boot edemez, OT edermiş** — host<->OT etkileşimi geliştiriliyor,
OT'nin cluster'ı nasıl boot ettiği tamamen black box (madde 2/3'ün
güncellemelerine bkz.). Bu nedenle `CARFIELD_CLUSTER_RUN`
(`driver/carfield.c`) artık register-poke yerine host->OT mailbox notify
yapıyor (`CARFIELD_MOCK_OT_CMD_CLUSTER_BOOT`, mbox1) ve cevabı mbox7
üzerinden bekliyor.

**Açık soru, kullanıcının kendi tahmini (kesin bilgi değil):** Bu mbox7
cevabı muhtemelen "cluster'ın çalışması bitti" anlamına geliyor — çünkü
cluster host'la doğrudan iletişime geçemiyor, dolayısıyla tamamlanma
bilgisini de OT'nin host'a iletmesi mantıklı. Ama bu **teyit edilmedi**:
- Mevcut donanımsal EOC IRQ yolu (`carfield_eoc_isr`/`CARFIELD_EOC_IRQ`,
  cluster'ın kendi `eoc_o` sinyali, madde 2) hâlâ var ve dokunulmadı, ama
  artık `CARFIELD_CLUSTER_RUN` bunu beklemiyor.
- Belirsiz: mbox7 cevabı GERÇEKTEN "cluster bitti" mi diyor, yoksa sadece
  "OT boot isteğini kabul etti" mi (ayrı, sonradan gelecek bir tamamlanma
  sinyali olabilir)? İkinci ihtimalde, gerçek tamamlanma sinyali hâlâ
  donanımsal EOC IRQ'dan mı gelecek (madde 2), yoksa OT ikinci bir mbox7
  mesajıyla mı bildirecek?

- Etkilenen kod: `driver/carfield.c` — `CARFIELD_CLUSTER_RUN` case'inin
  yorum bloğu bu belirsizliği işaretliyor; `pyiface/device.py`'deki
  `cluster_run()` docstring'i de aynı şekilde.
- Sorulacak: Daniele'ye — mbox7'nin `CLUSTER_BOOT` cevabı ne zaman
  gönderiliyor (boot isteği kabul edilir edilmez mi, yoksa cluster fiilen
  bitince mi)?

## Düşük öncelik / bilgi amaçlı (cevap beklemiyor)

- `MBOX_LETTER0`/`MBOX_LETTER1` register offset'leri (**0x80/0x84** —
  önceki `0x8C` şüphesi 2026-07-06'da gelen gerçek `mbox.h` ile
  düzeltildi, bkz. yukarı) — Daniele bunlarla uğraşmamıza gerek
  olmadığını, low-level API'lerle birlikte geleceğini söylemişti.
  Register-access katmanına fazla yatırım yapılmadı, bu doğru.
