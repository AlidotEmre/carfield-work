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

1. Mailbox'a yazmadan önce `fence.i` — cache flush zorunlu
2. Mailbox'a yazılan her adres `page_to_phys()` ile çevrilmeli — sanal adres donanımda çalışmaz
3. OpenTitan 32-bit adres alanında — `phys_addr & 0xFFFFFFFF`
4. User belleği pin'lenmeli — `get_user_pages()` ile

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
