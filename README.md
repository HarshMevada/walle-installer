# Wall-E Robot — Web Installer

Your own one-click installer page, like `huyvector.github.io/wall-e-ai-installer`:
**plug in USB → click Install → done.** GitHub builds the firmware for you in the cloud, so you
don't install ESP-IDF or Arduino.

When it's set up, your link is: **`https://YOUR-GITHUB-NAME.github.io/walle-installer/`**

---

## One-time setup (about 10 minutes of clicking + 20 minutes waiting)

### 1. Create the repository
1. Sign in at https://github.com (create a free account if you don't have one).
2. Click **+** (top right) → **New repository**.
3. Repository name: **`walle-installer`** · choose **Public** · click **Create repository**.

### 2. Turn on GitHub Pages (do this *before* uploading)
1. In the new repo: **Settings → Pages** (left menu).
2. Under **Build and deployment → Source**, choose **GitHub Actions**.

### 3. Upload the files
On the repo's main page click **"uploading an existing file"**, then drag **everything inside
this folder** (`.github`, `firmware`, `site`, `README.md`) onto the page and click
**Commit changes**.

> Make sure the `.github` folder is uploaded too: it contains the build recipe. If your file
> manager hides it, use the Git method below instead.

**Or with Git** (Git is already installed if you followed the main guide), from inside this folder:
```bash
git init -b main
git add .
git commit -m "Wall-E web installer"
git remote add origin https://github.com/YOUR-GITHUB-NAME/walle-installer.git
git push -u origin main
```

### 4. Wait for the build
1. Open the **Actions** tab. A run called **"Build Wall-E web installer"** starts by itself.
2. The first run takes about **15–20 minutes** (it downloads ESP-IDF libraries). A green ✓ means
   it worked.
3. Open **`https://YOUR-GITHUB-NAME.github.io/walle-installer/`**. That's your installer!

If the run shows a red ✗ at "Publish to GitHub Pages", Pages wasn't switched on yet: do step 2,
then on the failed run click **Re-run all jobs**.

---

## Installing on the robot

1. Open your installer link in **Chrome or Edge** on a computer.
2. Plug the XIAO in with a USB **data** cable.
3. Click **Connect & install Wall-E** → pick **"USB JTAG/serial debug unit"** → **Install**.
   (No port listed? Hold BOOT, tap RESET, release BOOT, try again.)
4. After about 2 minutes it restarts. Then:
   - join the **Xiaozhi-XXXX** Wi-Fi hotspot from your phone and set your 2.4 GHz Wi-Fi
   - enter the 6-digit code at **xiaozhi.me → Add device**
   - set the name **Wall-E**, a voice and a personality in the xiaozhi.me console

---

## Changing things later

| You want to… | Do this |
|---|---|
| Hindi or another language / other wake word | **Actions → Build Wall-E web installer → Run workflow** → fill in *Language* (e.g. `hi-IN`) and *Wake word* → **Run** |
| Change pins, invert a wheel, flip the screen | Open `firmware/walle-xiao-s3/config.h` on GitHub → ✏️ edit → **Commit**. It rebuilds automatically (~15 min) and the installer updates. |
| Check the build | **Actions** tab → open the latest run → its log |
| Download the raw .bin | **Actions** → a finished run → *Artifacts* → `walle-firmware` |

Useful `config.h` settings: `MOTOR_LEFT_INVERT` / `MOTOR_RIGHT_INVERT` (wheel goes backwards),
`MOTOR_LEFT_IS_A` (left/right swapped), `DISPLAY_MIRROR_X/Y` (upside-down screen),
`DISPLAY_I2C_ADDR` (0x3C or 0x3D).

---

## Good to know
- The repo has to be **Public** for free GitHub Pages, so anyone with the link can see the page.
  That's fine: it only contains your own Wall-E code.
- The **original Wall-E AI eye animations** and the **"Hi Wall-E" wake word** are Huy Vector's
  files, so they are **not** in this public repo. This installer uses the drawn Wall-E eyes. You
  can still flash the "Hi Wall-E" wake word yourself afterwards (see the main guide, Part I).
- What's inside: `firmware/` = Wall-E board code + patch, `site/` = the installer page,
  `.github/workflows/build-installer.yml` = the cloud build recipe (xiaozhi-esp32 at a fixed,
  tested version + ESP-IDF 6.1).
