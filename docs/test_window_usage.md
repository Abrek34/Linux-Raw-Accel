# Fare Kilit Test Penceresi — Kullanım Kılavuzu (P104 + P109)

Bu doküman, RawAccel Linux GUI'sindeki **fare kilit test penceresini** gerçek
makinede doğru kullanmak ve her parametre ailesi için hızlı bir **kabul testi
(A/B)** yapmak içindir. `docs/real_hardware_test.md`'deki uzun oturum
protokolünün *tamamlayıcısıdır*: buradaki pencere, o dokümandaki "his oturumu"
nun daha dar, tek pencereli, hız/kazanç sayacı olan bir parçasıdır.

**Bu pencere kime yarıyor:** fareyi test ederken imleç ekranın her yerine
fırlayıp sekmeleri, sayfaları ve pencereleri kaydıran kullanıcıya. Test
penceresi imleci kendi içine kilitler — **diğer pencereler hiçbir hareket,
kaydırma veya üzerine-gelme almaz**. Kullanıcının şikayeti tam olarak buydu:
> "mouse test edilirken bütün sayfaları birbirine sokuyor"

Pencere, bu karışıklığı kökünden çözer: test sırasında olan tek şey, kilit
penceresinin içindeki okuyucuda görünür; masaüstünün geri kalanı hiçbir olay
görmez.

---

## 1. Pencere kilit ne demek — ne kilitlenir, ne güvenilmez

Teknik olarak pencere, GTK4'ün kaldırdığı pointer grab API'sine başvurmadan
çalışır: P104 bunun yerine **tam ekran bir HUD** açar (`gui/mouse_test.inl`).
HUD bütün masaüstünü kapladığı için imlecin gidebileceği başka pencere yoktur —
"mouse test edilirken diğer pencereler karışmasın" şikayetinin çözümü tam da
budur. Tek monitörde imleç HUD'un dışına çıkamaz (klik/dokunmak için açık başka
hedef yoktur); çoklu monitörde HUD yalnız *etkin* monitörü kaplar (sınırlar
Bölüm 4'te). Bunun iki sonucu vardır:

| Etki | Kilit altındaki pencere | Geri kalan masaüstü |
|------|-------------------------|---------------------|
| İmleç hareketi | İçeride serbest | Hiçbir hareket ulaşmaz |
| Tıklama / seçim | Pencere içinde çalışır | Tıklama ÇIKMAZ — başka uygulama odak almaz |
| Kaydırma / tekerlek | Pencere içinde | Sayfalar kaymaz |
| Üzerine gelme (hover) | Yalnız test penceresi | Diğer pencereler hiçbir hover almaz |

Yani **test sırasında diğer sekmeler/pencerekler dokunulmaz** kalır — şikayet
edilen "sayfaları birbirine sokma" davranışı kilitliyken imkansızdır.

> **Güven sınırı:** Kilit yalnız **fare olaylarını** çevreler; klavye HUD'un
> kendisine gider. ESC kilidi bırakır; pencere yöneticisi kısayolları (Alt+Tab,
> Super+...) çalışmaya devam eder. Uygulama/oyun kısayolları test süresince
> hedefe ulaşmaz (hedef uygulama zaten HUD'un altında kapalıdır) — bu, test
> sırasında istenen davranıştır.

---

## 2. Pencereyi açma (menü yolu)

GUI'nin masaüstü menü çubuğu yoktur; her şey **başlık çubuğu (header bar) +
alt durum çubuğu** üzerindedir. Test penceresi alt durum çubuğundadır:

1. `rawaccel-gui`'yi başlatın, daemon'un `Aktif (running)` göründüğünden emin
   olun (durum yazısı başlık çubuğunun sağında).
2. Alt durum çubuğunda **"Performans"** düğmesinin hemen yanındaki
   **"Fare Testi"** düğmesine tıklayın (`Fare Testi`, "Performance"→"Performans"
   düğmesinin sağındaki son düğmedir).
3. **"Fare Kilit Testi"** başlıklı **tam ekran** bir HUD açılır; imleç kendini
   ekranın içinde bulur — masaüstünde başka pencere görünmez.

> **Çoklu monitör:** Tam ekran yalnız *etkin* monitörü kaplar; imleç monitör
> kenarından diğer monitöre taşabilir. Teste başlamadan önce hedef monitörü
> aktif edin (fareyi oraya taşıyıp HUD'u açın) — Bölüm 4.

Pencere ilk sayfada günlük kullanım yerine doğrudan test moduna girer:

```
Fare Kilit Testi                     ← pencere başlığı
Giriş (ips):  —                      ← input hızı (daemon telemetrisi, ips)
Çıkış (ips):  —                      ← output hızı (ips)
Kazanç (×):   —                      ← çıkış/giriş oranı; 1.00 = ham 1:1
Hareket bekleniyor…                  ← ilk örnek alınana kadar; sonra boşalır
İmleç bu tam ekran test penceresine kilitli.
Canlı hız/kazanç için fareyi hareket ettirin.
Kilit ESC ile bırakılır.
```

Okuma etiketleri ilk harekete kadar **"—"** ve alt satırda **"Hareket
bekleniyor…"** gösterir (daemon daha örnek yayınlamadı); ilk hareket
örneğiyle birlikte sayılar belirir ve alt satır boşalır.

Pencere açıkken **"Fare Testi" düğmesine tekrar tıklamak** pencereyi yeniden
öne getirir (ikinci bir pencere açmaz). Okumalar 250 ms'de bir yenilenir —
sayıların akması için fareyi **devamlı** hareket ettirin, tek seferlik bir
defa kıpırdatmak bir ya da iki örnek gösterir.

**Hangi profili test ediyoruz?** Pencere, daemon'un **o an aktif profilini
canlı okur** (250 ms'lik yoklama her seferinde daemon'a sorar). Profili
değiştirmek için ana pencereden hedef profili seçip **Apply** yapmanız yeterli
— HUD açık kalsa bile sayılar yeni profili göstermeye başlar.

---

## 3. Kabul protokolü — aile başına A/B (pencerenin İÇİNDE)

Her aile için prosedür aynıdır: profili aktifleştir → pencereyi aç → aşağıdaki
hız aralıklarında fareyi **fısıl fısıl** (micro-aim) ve **hızlı çekim** (flick)
yap → **"Kazanç (×)"** okumasını beklenen değerle karşılaştır.

| Aile / mod | Profil (öneri) | Beklenen davranış | Kabul kriteri |
|------------|----------------|-------------------|---------------|
| **classic (gain)** | `cs2`, `fps`, `gaming` | Yavaşta 1:1'e yakın, hızla birlikte gain tırmanır, çatı (limit/cap) üstüne çıkmaz | `cs2`: yavaş 1.00–1.01 → 1.32 @80 ips → **max 1.58** @900 civarı; `fps`: 1.00–1.01 → 1.40 @80 → **max 1.76**; asla limiti (1.6 / 1.8) aşmaz |
| **power (cap hit)** | `apex` | Çok erken tırmanma, çatıya hızlı oturma (180° hızı için bilinçli) | ~0.90 zemin → **2 ips'te ~1.95** → 5 ips'te ~2.10 → orta bölge (10–30 ips) 2.15–2.18 → **hızlı çekimde ~2.20 çatı** (2.2'yi asla aşmaz) |
| **natural (smooth)** | `valorant`, `office` | Dar bantta yumuşak giriş/çıkış | Kazanç her hızda **1.0–1.3 arası**; 0.2→1.01, 2→1.07, 30→1.26, 900→1.30; ani zıplama olmaz |
| **noaccel (1:1)** | `default` (taze kurulum) veya noaccel profil | Hiçbir hızda ivme yok | **Kazanç her hızda 1.00** (ekrandaki kayan yazı 1.00'dan sapmaz) |
| **raw passthrough** | `disable` | Hız/gain hattı devre dışı (modifier yok) | **Beklenen: okuma 0.00 (veya "Hareket bekleniyor…")** kalır — bu bir hata değil; 1:1'i *elle* teyit edin (aşağıya bakın) |
| **lookup (LUT)** | GUI modu "Arama (LUT)" | gain = LUT çıkış / giriş (nokta = (giriş, çıkış)) | Basit LUT `[(10,10),(100,200)]` için: 10 ips'te Kazanç ≈ **1.00**, 100 ips'e yaklaştıkça ≈ **2.00**; LUT yeniden sıralandıkça okuma düzgün izler |

**Noaccel'le raw'ı birbirine karıştırmayın:**

- **noaccel** (ör. taze kurulum `default` profili): ivme kapalı ama olaylar
  hız hattından (flush_motion) geçer → pencere **Kazanç 1.00** gösterir,
  "1:1" teyidi budur.
- **`disable`** preseti `raw_passthrough = on` olduğundan **tüm hız/gain
  hattını baypas eder** → pencere okuyucusu 0.00 kalır. Bu durumda 1:1'i
  **elle** doğrulayın (imleç elinizle birebir gidiyor mu), kazanç okumasına
  bakmayın. Bu, `docs/real_hardware_test.md` Bölüm 2'deki "raw ≠ noaccel"
  notu ile aynı gerçektir.

> Okuma **≈** işaretli hızlarda 0.02–0.05 mertebesinde oynarsa normaldir:
> pencere 250 ms'de bir örnek alır ve fırça hızı (flick) tam 80/900 ips
> olamaz. Hızlı çekimi birkaç kez tekrarlayıp en yüksek gördüğünüz kazancı
> tablodaki tavanla karşılaştırın. Beklenen sayılar `include/presets.hpp`
> `make_preset()`'ten türetilmiştir (Bölüm 3.3'teki tablo ile birebir).

**Prosedür (bir aile için):**

1. Ana pencere → profil seç → Apply (daemon'a canlı uygular).
2. "Fare Testi" → pencere kilitlenir.
3. **Yavaş: 2–20 mm/s** el hızıyla mikro düzeltmeler → kazancı oku.
4. **Flick: hızlı 180°/panik çekimi** → kazancın tepe değerini oku.
5. Beklenenle karşılaştır; fark sistematikse `docs/user_guide.md`'deki tuning
   notlarına bak, DPI/duyarlılık etkisini ayıklamak için
   `docs/real_hardware_test.md` Bölüm 3.2 DPI merdivenini uygula.

---

## 4. ESC kaçış kapağı ve kilit sınırları (sorun giderme)

Kilidi bırakmanın garantili yolu **ESC**'dir:

- `ESC` → HUD kapanır, imleç serbesttir; ana GUI durum çubuğunda
  **"Fare testi: imleç serbest bırakıldı (ESC)."** yazar.
- `Alt+F4` / görev çubuğundan kapat gibi pencere yöneticisi kapatma istemleri de
  aynı GTK destroy yolundan geçer ve teardown aynıdır (tam ekranda başlık çubuğu
  görünmediği için × tuşu her zaman erişilemez).

**P104'te "yakalama başarısız" diye bir yol yoktur** çünkü gerçek bir grab
yoktur (GTK4 pointer grab API'sini kaldırdı; tam ekran HUD emülasyonu başarısız
olacak bir grab adımı içermez). "Kilit tutmadı" hissinin gerçek nedenleri ve
çözümleri şu tablodadır:

| Belirti | Olası neden | Ne yapmalı |
|---------|-------------|------------|
| İmleç HUD açıkken diğer monitöre taşıyor, oradaki pencereleri kaydırıyor | Çoklu monitör: fullscreen tek monitörü kaplar (GTK davranışı) | Test monitörünü öne alın (HUD'u orada açın); diğer monitördeki uygulamaları kapatın veya (KWin) pencereye "tüm ekranlara yay" verin |
| X11 + pencere yöneticisi fullscreen'i onurlamıyor (nadir) | WM tam ekran ipucunu yok sayabilir; ekranın bir bölümü açık kalır | Alt+F9/KDE tam ekran kısayoluyla yeniden dene; WM'yi değiştir; en kötü ihtimalle testi Wayland oturumunda yap (fullscreen orada kompozitör garantilidir) |
| Wayland'da HUD ekranı kaplamıyor / komşu ekran karışıyor | Çoklu monitör / kompozitör ölçekleme | KWin "fullscreen tüm çıkışları kaplasın" davranışını kontrol edin; ESC → yeniden açın |
| Sayılar hiç gelmiyor (— / alt satırda "Hareket bekleniyor…") | Daemon kapalı veya cihaz telemetri üretmiyor | **"Daemon çalışmıyor."** yazısına bakın; `rawaccel-cli status` ile cihazın grab edildiğinden emin olun |
| Okuma var ama 1:1 hissi yok | Yanlış/raw profil aktif (`disable`) | Bölüm 3'e göre doğru profili Apply edin; `rawaccel-cli status` → `Active:` kontrolü |

Her koşulda **ESC çalışır** ve telemetri **grab'a değil daemon'a** bağlıdır:
HUD'un görünüşü ne olursa olsun hız/kazanç okuması daemon'dan gelir.

**Daemon kapalıysa** pencere **"Daemon çalışmıyor."** gösterir — hız/gain
okunamaz; daemon'u çalıştırın, pencere 250 ms'lik yoklamayla kendini toparlar
(pencerenin açık kalması sorun değildir).

---

## 5. 5 dakikalık "her şey açık" güvenlik matrisi

Test sonrası kilidi bırakın (ESC) ve 5 dakika normal masaüstü kullanın. Kilit
KAPALIYKEN aşağıdakilerin **olması beklenir** — bunlar test penceresinin
kaplamadığı, normal sistem davranışıdır ve hata sayılmaz:

| Davranış | Beklenen mi? | Neden | Ne yapmalı |
|----------|--------------|-------|------------|
| İmleç artık ekranın her yerine gidiyor | **Evet** | HUD kapanınca tam ekran örtü yok; kilit yalnız test süresinceydi | Doğrunun ta kendisi — geçti |
| Sekmeler/pencerekler imleçle kayıyor | **Evet** | Kilit yokken sistem normal davranır | Beklenir; yeniden test için pencereyi açın |
| Masaüstü köşe/kısayol köşeleri (hot corner) tetikleniyor | **Evet** | Kilit yokken pointer köşelere ulaşabilir; köşe kısayolları sistem seviyesindedir | Beklenir |
| KDE'de fare hızlandırması yeniden "çift" gibi | **Evet**, yalnız düzeltme kapalıysa | `kde-fix-accel.sh` zaten uygulanmadıysa masaüstü kendi ivmesini katar | `bash scripts/kde-fix-accel.sh` sonra yeniden test |
| Oyun içi "Raw Input" ayarı değişti | **Hayır — beklenmez** | Daemon oyuna yazmaz, oyunun ayarı değişmez | Değiştiyse bir başka uygulama dokundu demektir; oyunu kapıp açın |
| `disable` profiliyle kazanç 0.00 | **Evet** | Raw passthrough hız hattını baypas eder (Bölüm 3) | Doğru; 1:1'i elle teyit edin |

**Hata sayılan tek şey:** kilit penceresi AÇIKKEN diğer pencerelerde
hareket/kaydırma/hover görülmesi. Onu görürseniz:

1. `rawaccel-cli status` → hangi cihazlar grab ediliyor, hangi profil aktif?
   (raw 1:1 `disable`'da kazanç 0.00'da — o bir hata değil, Bölüm 3.)
2. Çoklu monitör mü? → Bölüm 4'teki kilit sınırları tablosu (fullscreen tek
   monitörü kaplar). HUD'u hedef monitörde açın; o monitörde başka pencere
   olmadığından emin olun.
3. İkisi de değilse → bunu Aj 1'e bildirin: P104 kilitleme davranışı
   kabul dışı bulundu.

---

## 6. Otomatik karşı kontrol (isteğe bağlı, VM/ana makine)

Kilit penceresinin ürettiği *hareket imzasını* daemon üzerinde yalnız başına
doğrulamak için `scripts/virtmouse-game.c`'nin `locked` senaryosu kullanılabilir
(gerçek fare gerekmez — yalnız daemon ölçeği):

```bash
# (input grubu üyesi olarak) kilitlenmiş imleç imzasını 10 sn enjekte et:
gcc -O2 -o build-manual/virtmouse-game scripts/virtmouse-game.c
build-manual/virtmouse-game locked 10     # 90×60 px kutu, 1000 Hz, re-wrap'lı koordinatlar

# sonra canlı histogram:
rawaccel-cli latency
journalctl -u rawaccel -n 30
```

Senaryo, 90×60 px'lik kutunun içinde kalan ve kenara değince karşı duvara
re-wrap olan koordinat akışını 1000 Hz'de üretir — gerçek kilitli imlecin
pencere ölçeğinde yaptığına birebir benzer. p50/p95/p99 referans değerleriyle
karşılaştırma `docs/real_hardware_test.md` Bölüm 4.2'dedir; aile başına aynı
ölçümün cs2/valorant/apex/fps referans satırları Bölüm 4.4'teki tablodadır.

**P109 canlı ölçüm (bu VM, cs2 preset `rtp_cs2` aktif, 10 sn × 1000 Hz):**
`locked` 10 sn → 777 box-edge re-wrap; daemon histogramı (4661 örnek):
Min 1.12 µs · **Avg 4.21 µs · p50 3.75 µs · p95 5.75 µs · p99 8.25 µs** ·
Max 1656.88 µs. p50/avg ≈ 125 µs çerçeve bütçesinin ~1/30'u; **Max/over-flow'daki
(2 örnek >500 µs) tepe noktaları re-wrap "büyük düzeltme delta"sı ve geç uyanan
1000 Hz döngüsünün toplu işlemidir** — kilitli imlecin beklenen imzası, hata
değil.

---

*İlgili dosyalar: `gui/mouse_test.inl` (pencere, P104), `docs/real_hardware_test.md`
(his oturumu protokolü), `scripts/virtmouse-game.c` (`locked` senaryo, P109).*