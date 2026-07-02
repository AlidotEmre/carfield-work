# Sorulacaklar — Daniele toplantısı

Toplantı: önümüzdeki hafta Pazartesi veya Cuma 15:00 (tarih henüz teyitli değil).
Aşağıdakiler kod tarafında yer tutucu/varsayım olarak bırakılmış, gerçek
donanım/RTL bilgisi olmadan ilerlenemeyen noktalar. Her madde hangi kod
konumunu etkilediğiyle birlikte.

## 1. Mailbox: `INT_SND_EN`'i kim yazıyor?

Doorbell'ın `INT_SND_SET` olduğu netleşti (bkz. madde altında), ama bir
inbound mailbox'ın (mbox 5, mbox 7) kesmesini kimin **enable** ettiği
(receiver kendi `INT_SND_EN`'ini mi yazıyor, yoksa gönderen taraf mı)
netleşmedi.

- Etkilenen kod: `carfield_mbox_in_init()` —
  `mailbox-simulation-withoutFPGA/carfield_mbox.c`. Şu an receiver'ın
  kendi `INT_SND_EN`'ini yazdığı varsayılıyor (host, kendi mbox 5/7
  handle'ında). Yanlışsa tek satır değişir ama yön netleşmeli.

## 2. EOC kesmesinin kesin PLIC source ID'si

PULP cluster'ın `eoc_o` sinyalinin host'a `intr_ext_i[0]` (`pulpcl_eoc`,
level-sensitive) olarak ulaştığı Carfield/Cheshire mimari dokümanlarından
doğrulandı, ama bunun PLIC üzerindeki **kesin sayısal source ID'si**
(Cheshire internal+external kesme birleştirme sırasına bağlı) public
dokümanlarda yok.

- Etkilenen kod: `driver/carfield.c` — `CARFIELD_EOC_IRQ` şu an `0`
  (bilinçli yer tutucu, `request_irq()` bu değer için hiç çağrılmıyor).
  Gerçek numara gelince tek satır değişecek.

## 3. PULP Event Unit (EU) — canlı gösterim + register API

PULP'a komuta giden bir mailbox yok; tüm komut/senkronizasyon yolu Event
Unit üzerinden olacak. Bu, mailbox katmanından tamamen ayrı, kernel
driver tarafında henüz hiç başlanmadı.

- Elimde `hal/eu/eu_v3.h` (pulp-sdk) var ama bu **cluster-side** HAL
  (PULP core'larının kendi EU'sunu kullanması) — host-side (Linux
  driver'ın PULP'a nasıl iş vereceği) tarafı tamamen ayrı ve bilinmiyor.
- Sorulacak: Host'tan PULP'a EU üzerinden nasıl tetikleme yapılıyor?
  Hangi register/adres, hangi mekanizma?

## 4. Cache coherence — CVA6 ↔ PULP/OpenTitan

Header/map/data sayfaları CVA6'nın cache'i üzerinden yazılıyor;
OpenTitan/PULP bunları doğrudan RAM'den okuyacak. İnterconnect coherent
değilse doorbell'dan önce bir cache flush (fence.i benzeri) gerekiyor —
CLAUDE.md'deki "her zaman fence.i" kuralı muhtemelen bunun için ama hangi
API/instruction'ın (ve tam olarak hangi bellek bölgelerinin) gerektiği
netleşmedi.

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
canlı olarak tekrar kontrol edilecek (Daniele'nin kendi önerisi).

---

## Düşük öncelik / bilgi amaçlı (cevap beklemiyor)

- `MBOX_LETTER0`/`MBOX_LETTER1` register offset'leri (0x80/0x8C) —
  Daniele bunlarla uğraşmamıza gerek olmadığını, low-level API'lerle
  birlikte geleceğini söylemişti. Register-access katmanına
  (`mbox_reg()`) fazla yatırım yapılmadı, bu doğru.
