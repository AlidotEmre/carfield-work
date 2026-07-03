
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
├── memory/                 ← Claude hafıza dosyaları (otomatik yüklenir)
│   ├── MEMORY.md
│   └── project_alsaqr.md
├── driver/                 ← kernel driver kodu (Aşama 0'dan itibaren)
│   ├── carfield.c/.h           ← /dev/carfield, IOCTL'ler, mmap, EOC IRQ
│   ├── carfield_paging.c/.h    ← header/map page zinciri, pin/unpin
│   └── carfield_paging_math.c  ← sayfa-düzeni matematiği (kernel'siz derlenir)
├── sw/                     ← PULP tarafı test kodu (pulp_hello.c)
└── tests/                  ← userspace testler (ioctl_test, cluster_test,
                                paging_math_test, paging_ioctl_test)
```

---

## Faydalı Linkler

- [Carfield GitHub](https://github.com/pulp-platform/carfield)
- [Linux Kernel Labs](https://linux-kernel-labs.github.io/refs/heads/master/labs/device_drivers.html)
- [Carfield Paper (DAC 2023)](https://pulp-platform.org/docs/dac2023/Carfield_SSH_SoC_DAC23_v2_pdf.pdf)
