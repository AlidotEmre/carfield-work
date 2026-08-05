
# carfield-work

Academic Research Assistant staj projesi — PoliTo Energy Center, İtalya  
Başlangıç: 22 Haziran 2026

## Proje Özeti

Carfield SoC üzerinde encrypted TinyML inference çalıştıran Linux kernel driver.

```
Python (user space)
    │ ioctl
    ▼
/dev/alsaqr  ← BU DRIVER (senin işin, alsaqr-migration branch)
    └── Mailbox ─────▶ OpenTitan   (Luca + Francesco + Tina)
```

**Önemli (2026-08-05, `alsaqr-migration` branch):** Cluster-boot desteği
(`ALSAQR_CLUSTER_RUN`, L2/soc_ctrl/safety-island/spatz mmap bölgeleri, EOC
IRQ) bu branch'ten kaldırıldı — AlSaqr'ın gerçek device-tree'sinde
(`alsaqr-fpga-ecs/dts/generate_dts.py`), kendi SDK'sının boot akışında
(`alsaqr-fpga-ecs/Readme.md`) ve titanssl driver'da (tek gerçek donanık
referans) bu bölgelere/bir PULP cluster'a dair hiçbir iz yok — Carfield'ın
kendi Cheshire tabanlı host-SoC'sinden miras kalmış, AlSaqr'da karşılığı
teyit edilemeyen kod. Detay: `memory/project_alsaqr.md`, `CLAUDE.md`
branch notu. Eski Carfield mimarisi (host artık cluster'ı doğrudan boot
edemiyor, OT ediyor) hâlâ `main` branch'inde geçerli, sadece bu koddan
kaldırıldı çünkü AlSaqr'da bir PULP cluster olduğuna dair kanıt yok.

---

## Yeni Cihazda Kurulum

### 1. Bu repoyu clone'la
```bash
git clone https://github.com/AlidotEmre/carfield-work.git
cd carfield-work
```

### 2. Claude hafızasını bağla
```bash
mkdir -p ~/.claude/projects/-home-ubuntu/memory
# Eğer dizin zaten varsa önce sil:
# rm -rf ~/.claude/projects/-home-ubuntu/memory
ln -s ~/carfield-work/memory ~/.claude/projects/-home-ubuntu/memory
```

### 3. Lab referans reposunu clone'la (lab GitLab erişimin gerekli)
```bash
git clone https://gitlab.com/ecs-lab/private/alsaqr/alsaqr-fpga-ecs.git
```

Kritik referans dosyalar:
```
alsaqr-fpga-ecs/develop/titanssl/titanssl_driver/driver.c   ← ANA REFERANS
alsaqr-fpga-ecs/develop/titanssl/titanssl_driver/titanssl.h
alsaqr-fpga-ecs/supt-openssl-ecslab/driver/kthread.c        ← HW simülasyonu
alsaqr-fpga-ecs/supt-openssl-ecslab/tests/ioctl_test.c
```

### 4. Carfield reposunu clone'la (Aşama 1'den itibaren)
```bash
git clone https://github.com/pulp-platform/carfield.git
```

---

## Her Session Sonunda

```bash
cd ~/carfield-work
git add .
git commit -m "session: <tarih> - <kısa not>"
git push
```

## Her Yeni Session Başında (yeni cihazda)

```bash
cd ~/carfield-work && git pull
```

---

## Repo Yapısı

```
carfield-work/
├── README.md               ← bu dosya
├── CLAUDE.md               ← Claude direktifleri
├── docs/                   ← spec/analiz dokümanları
│   ├── QUESTIONS_FOR_TEAM.md   ← toplantı gündemi (yer tutucu/varsayım listesi)
│   ├── MOCK_OT_SPEC.md         ← mock OpenTitan consumer kontratı
│   ├── PYIFACE_SPEC.md         ← Python arayüz katmanı kontratı (Aşama 4)
│   └── TITANSSL_ANALYSIS.md    ← titanssl referans kodu incelemesi
├── memory/                 ← Claude hafıza dosyaları (otomatik yüklenir)
│   ├── MEMORY.md
│   └── project_alsaqr.md
├── driver/                 ← kernel driver kodu (Aşama 0'dan itibaren; alsaqr-migration
│   │                              branch'inde AlSaqr'a yeniden adlandırıldı VE Cheshire/Carfield'a
│   │                              özgü cluster-boot/mmap/EOC IRQ kodu kaldırıldı, bkz. CLAUDE.md)
│   ├── alsaqr.c/.h             ← /dev/alsaqr, IOCTL'ler (PING + host↔OT paging/mailbox)
│   ├── alsaqr_paging.c/.h      ← header/map page zinciri, pin/unpin (scattered veri transferi)
│   ├── alsaqr_paging_math.c    ← sayfa-düzeni matematiği (kernel'siz derlenir)
│   ├── alsaqr_mock_ot.c/.h     ← mock OpenTitan consumer (kthread) + host<->OT ioctl/cmd sözleşmesi
│   │                              (ALSAQR_OT_XFORM/ALSAQR_OT_CMD_XFORM — hem mock hem gerçek
│   │                              donanım backend'i bunları paylaşır)
│   └── alsaqr_mbox_hw.c/.h     ← GERÇEK mailbox donanım backend'i (real_mbox=1), AlSaqr'ın gerçek
│                                  device-tree'sinden (base 0x10404000, DT-probe'lu IRQ) register
│                                  haritası, aynı seam'in ikinci implementasyonu (kthread değil)
├── pyiface/                ← Python arayüz katmanı (Aşama 4, bkz. docs/PYIFACE_SPEC.md)
│   ├── abi.py                  ← TEK donanım aynası: ioctl no, ctypes struct'lar, errno mapping
│   ├── device.py               ← AlsaqrDevice: op başına metot (ping/paging_test)
│   └── demo.py                 ← MOCK-ONLY: xform() (ALSAQR_OT_XFORM demo'su)
└── tests/                  ← userspace testler
    ├── ioctl_test.c / paging_math_test.c / paging_ioctl_test.c / mock_ot_test.c  (C)
    ├── mbox_reg_test.c          ← alsaqr_mbox_hw.h'nin register-map aritmetiği (kernel'siz derlenir)
    ├── conftest.py              ← pyiface/'i sys.path'e ekler
    └── test_pyiface.py          ← docs/MOCK_OT_SPEC.md §7'nin Python karşılığı
```

---

## Driver'ı Derle ve Yükle

```bash
cd ~/carfield-work/driver && make
```

**Modül parametreleri (birbirini dışlar — ikisi de set edilirse `alsaqr_init()` ikisini de başlatmaz):**

| Parametre | Ne yapar |
|---|---|
| `mock_ot=1` | Host↔OT mailbox seam'ini bir kthread mock'la test eder — FPGA gerekmez |
| `real_mbox=1` | Aynı seam'i gerçek mailbox register'ları üzerinden gerçek donanıma bağlar — AlSaqr'ın gerçek device-tree düğümüne (`compatible = "opentitan_mbox-0.0"`, base `0x10404000`) `platform_driver` ile eşleşip IRQ'yu oradan dinamik alır (artık hardcoded değil) |
| (ikisi de 0) | Sadece `ALSAQR_PING`/`ALSAQR_PAGING_TEST` kullanılabilir, host↔OT ioctl'i (`ALSAQR_OT_XFORM`) `-ENODEV` döner |

```bash
sudo insmod alsaqr-mod.ko mock_ot=1     # ya da: real_mbox=1
```

`tests/` altındaki binary'ler `.gitignore`'da — `git pull` sonrası
`alsaqr.h` değiştiyse **mutlaka** `cd tests && make clean && make`
(aksi halde sessiz `ENOTTY`).

Açık sorular ve donanım-bağımlı yer tutucular için `docs/QUESTIONS_FOR_TEAM.md`'ye bakın
(not: bu dosya hâlâ Carfield'ın cluster-boot sorularını içeriyor — `alsaqr-migration`
branch'i için artık geçerli değiller, bkz. `CLAUDE.md` branch notu).

---

## Python Arayüz Katmanı (`pyiface/`)

Kontrat: `docs/PYIFACE_SPEC.md`. Sadece stdlib kullanır (`ctypes`/`mmap`/`fcntl`), harici bağımlılık yok.

**Çalıştırma:**
```bash
cd ~/carfield-work/driver && make
sudo insmod alsaqr-mod.ko mock_ot=1
cd ~/carfield-work
sudo python3 -m pytest tests/test_pyiface.py -v
```

**GC tuzağı:** `AlsaqrDevice.alloc()`'un döndürdüğü `mmap` nesnesi, `addr` kullanılırken (ör. bir ioctl çağrısı boyunca) referans olarak tutulmalı — bırakılırsa anonim mapping geri alınabilir ve `addr` geçersiz hâle gelir ya da başka bir şeye yeniden atanır. `AlsaqrDevice` bu referansı çağıran adına tutmuyor (kasıtlı — aksi hâlde test suite'inin `gc.collect()` senaryolarının yakalamaya çalıştığı tam da bu tuzağı maskelemiş olurdu).

**Gerçek donanım netleştiğinde ne değişir (docs/PYIFACE_SPEC.md §6):**

| Bekleyen cevap | Python etkisi |
|---|---|
| Doorbell register, header→L2, map'in nerede olduğu | yok |
| L2 kapasitesi / transfer tavanı | `abi.py`'de tek bir sabit (+ `AlsaqrSizeError` eşiği) |
| Yeni gerçek op'lar (load model, run inference) | yeni ioctl numarası + yeni request struct + yeni metot |
| Request-struct evrimi (in/out ayrımı) | sadece `abi.py`'deki struct tanımları |
| Sonuç-dönüş yolu (host mu PULP L2 mi) | hangi metot gerektiriyorsa ona yeni çıktı-işleme — dokunmayan metotlar değişmez |

---

## Faydalı Linkler

- [Carfield GitHub](https://github.com/pulp-platform/carfield)
- [Linux Kernel Labs](https://linux-kernel-labs.github.io/refs/heads/master/labs/device_drivers.html)
- [Carfield Paper (DAC 2023)](https://pulp-platform.org/docs/dac2023/Carfield_SSH_SoC_DAC23_v2_pdf.pdf)
