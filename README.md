
# carfield-work

Academic Research Assistant staj projesi — PoliTo Energy Center, İtalya  
Başlangıç: 22 Haziran 2026

## Proje Özeti

Carfield SoC üzerinde encrypted TinyML inference çalıştıran Linux kernel driver.

```
Python (user space)
    │ ioctl
    ▼
/dev/carfield  ← BU DRIVER (senin işin)
    ├── Mailbox ──▶ PULP Cluster  (Giovanni + Daniele)
    └── Mailbox ──▶ OpenTitan     (Luca + Francesco + Tina)
```

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
├── QUESTIONS_FOR_DANIELE.md ← toplantı gündemi (yer tutucu/varsayım listesi)
├── MOCK_OT_SPEC.md         ← mock OpenTitan consumer kontratı
├── PYIFACE_SPEC.md         ← Python arayüz katmanı kontratı (Aşama 4)
├── memory/                 ← Claude hafıza dosyaları (otomatik yüklenir)
│   ├── MEMORY.md
│   └── project_alsaqr.md
├── driver/                 ← kernel driver kodu (Aşama 0'dan itibaren)
│   ├── carfield.c/.h           ← /dev/carfield, IOCTL'ler, mmap, EOC IRQ
│   ├── carfield_paging.c/.h    ← header/map page zinciri, pin/unpin
│   ├── carfield_paging_math.c  ← sayfa-düzeni matematiği (kernel'siz derlenir)
│   └── carfield_mock_ot.c/.h   ← mock OpenTitan consumer (kthread)
├── pyiface/                ← Python arayüz katmanı (Aşama 4, bkz. PYIFACE_SPEC.md)
│   ├── abi.py                  ← TEK donanım aynası: ioctl no, ctypes struct'lar, errno mapping
│   ├── device.py               ← CarfieldDevice: op başına metot (ping/cluster_run/paging_test)
│   └── demo.py                 ← MOCK-ONLY: xform() (CARFIELD_MOCK_OT_XFORM demo'su)
├── sw/                     ← PULP tarafı test kodu (pulp_hello.c)
└── tests/                  ← userspace testler
    ├── ioctl_test.c / cluster_test.c / paging_math_test.c / paging_ioctl_test.c / mock_ot_test.c  (C)
    ├── conftest.py              ← pyiface/'i sys.path'e ekler
    └── test_pyiface.py          ← MOCK_OT_SPEC.md §7'nin Python karşılığı
```

---

## Python Arayüz Katmanı (`pyiface/`)

Kontrat: `PYIFACE_SPEC.md`. Sadece stdlib kullanır (`ctypes`/`mmap`/`fcntl`), harici bağımlılık yok.

**Çalıştırma:**
```bash
cd ~/carfield-work/driver && make
sudo insmod carfield-mod.ko mock_ot=1
cd ~/carfield-work
sudo python3 -m pytest tests/test_pyiface.py -v
```

**GC tuzağı:** `CarfieldDevice.alloc()`'un döndürdüğü `mmap` nesnesi, `addr` kullanılırken (ör. bir ioctl çağrısı boyunca) referans olarak tutulmalı — bırakılırsa anonim mapping geri alınabilir ve `addr` geçersiz hâle gelir ya da başka bir şeye yeniden atanır. `CarfieldDevice` bu referansı çağıran adına tutmuyor (kasıtlı — aksi hâlde test suite'inin `gc.collect()` senaryolarının yakalamaya çalıştığı tam da bu tuzağı maskelemiş olurdu).

**Gerçek donanım netleştiğinde ne değişir (PYIFACE_SPEC.md §6):**

| Bekleyen cevap | Python etkisi |
|---|---|
| Doorbell register, header→L2, map'in nerede olduğu | yok |
| L2 kapasitesi / transfer tavanı | `abi.py`'de tek bir sabit (+ `CarfieldSizeError` eşiği) |
| Yeni gerçek op'lar (load model, run inference) | yeni ioctl numarası + yeni request struct + yeni metot |
| Request-struct evrimi (in/out ayrımı) | sadece `abi.py`'deki struct tanımları |
| Sonuç-dönüş yolu (host mu PULP L2 mi) | hangi metot gerektiriyorsa ona yeni çıktı-işleme — dokunmayan metotlar değişmez |

---

## Faydalı Linkler

- [Carfield GitHub](https://github.com/pulp-platform/carfield)
- [Linux Kernel Labs](https://linux-kernel-labs.github.io/refs/heads/master/labs/device_drivers.html)
- [Carfield Paper (DAC 2023)](https://pulp-platform.org/docs/dac2023/Carfield_SSH_SoC_DAC23_v2_pdf.pdf)
