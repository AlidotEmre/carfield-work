# Proje Direktifleri — Carfield Kernel Driver

## Bağlam

Bu repo PoliTo Energy Center staj projesi içindir. Kullanıcı Academic Research Assistant olarak
Carfield SoC için Linux kernel driver yazıyor. Proje başlangıcı: 22 Haziran 2026, İtalya.

## Ekip

| Kişi | Rol | Sorumluluk |
|---|---|---|
| Luca Barbierato | Asst. Prof. | Danışman |
| Francesco Barchi | Asst. Prof. | Danışman |
| Giovanni | PhD | PULP cluster bare-metal |
| Daniele | PhD | PULP cluster bare-metal |
| Tina | Yüksek lisans | Neural Network + Encryption |
| Kullanıcı | Research Asst. | Linux kernel driver |

## Çalışma Kuralları

- Kullanıcı Türkçe konuşur, Türkçe cevap ver.
- Kernel driver kodu yazarken her zaman `alsaqr-fpga-ecs/develop/titanssl/titanssl_driver/driver.c`
  dosyasını referans al — orada çalışan implementasyon var, tekerleği yeniden icat etme.
- FPGA mevcut değilse `supt-openssl-ecslab/driver/kthread.c` simülasyon yolunu öner.
- Teknik önerilerde Carfield memory map henüz netleşmediğinde varsayım yapma, belirt.
- Kod önerilerinde Linux kernel stil kurallarına uy (`checkpatch.pl` uyumlu).
- Her yeni teknik bilgi (memory map, boot prosedürü, API değişikliği) öğrenildiğinde
  `memory/project_alsaqr.md` dosyasını güncelle.

## Kritik Teknik Kurallar (Unutturma)

1. **`fence.i` cache flush YAPMAZ — bu kural yanlıştı, düzeltildi.** RISC-V'de
   `fence.i` instruction-cache/self-modifying-code senkronizasyonu içindir,
   veri belleği (data memory) barrier'ı değil. titanssl referansı
   (`titanssl_driver/driver.c:379`) doorbell'dan önce bunu "Clean cache!"
   yorumuyla çağırıyor ama bu muhtemelen bir yanlış anlamaydı — bağımsız
   doğrulanmış bir gereksinim değil (bkz. `docs/TITANSSL_ANALYSIS.md` §3).
   CVA6↔mailbox yolunun gerçekten cache-coherent olup olmadığı ve
   doorbell'dan önce gerçek bir veri fence'i/CMO gerekip gerekmediği AÇIK
   SORU — Daniele toplantısını bekliyor (bkz. `docs/QUESTIONS_FOR_TEAM.md`
   madde 4). Netleşene kadar yeni kodda `fence.i`'yi (ya da başka bir
   barrier'ı) "zaten böyle yapılıyor" diye körü körüne ekleme.
2. Mailbox'a yazılan her adres `page_to_phys()` ile çevrilmeli — sanal adres donanımda çalışmaz
3. OpenTitan 32-bit adres alanında — adres 4GB'ı aşıyorsa maskeleme değil, hata
   döndür (bkz. `carfield_paging.c`, `-ERANGE`); header/map gibi kernel
   tahsisleri `GFP_DMA32` ile yapılır, `& 0xFFFFFFFF` ile sessiz kırpma YOK
4. User belleği pin'lenmeli — `pin_user_pages_fast()` ile (`get_user_pages()`
   değil; `unpin_user_pages()` ile eşleşir)

## Referans Dosyalar

```
~/alsaqr-fpga-ecs/develop/titanssl/titanssl_driver/driver.c   ← ANA REFERANS
~/alsaqr-fpga-ecs/develop/titanssl/titanssl_driver/titanssl.h ← struct tanımları
~/alsaqr-fpga-ecs/supt-openssl-ecslab/driver/kthread.c        ← HW simülasyonu
~/alsaqr-fpga-ecs/supt-openssl-ecslab/tests/ioctl_test.c      ← test şablonu
```

## Aşamalar

- **Aşama 0** — Kernel module + `/dev/carfield` + IOCTL iskeleti (FPGA gerekmez)
- **Aşama 1** — Carfield repo inceleme, memory map ve boot prosedürü
- **Aşama 2** — Hello World in PULP cluster
- **Aşama 3** — Driver entegrasyonu
- **Aşama 4** — Python arayüzü (ctypes/cffi)
