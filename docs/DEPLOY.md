# Deploying the Documentation Site

## Quick Start (Local Preview)

```bash
cd docs
pip install -r requirements.txt
mkdocs serve
```

Open `http://127.0.0.1:8000` to preview the site locally.

## Publishing to GitHub Pages

### 1. Create a GitHub Repository

Create a new repo (e.g., `SeinARTSFramework` or `seinarts-docs`) and push the `docs/` folder contents:

```bash
cd docs
git init
git add .
git commit -m "Initial documentation site"
git remote add origin https://github.com/YOUR_USERNAME/YOUR_REPO.git
git push -u origin main
```

Alternatively, if the docs live inside the plugin repo, push the entire repo and the GitHub Action will handle the rest.

### 2. Enable GitHub Pages

1. Go to **Settings > Pages** in your repo
2. Under **Build and deployment**, set **Source** to **GitHub Actions**
3. The included workflow (`.github/workflows/deploy-docs.yml`) will build and deploy automatically on push to `main`

### 3. Verify Deployment

After the first push, the Action will run and deploy to:
```
https://YOUR_USERNAME.github.io/YOUR_REPO/
```

## Custom Domain Setup

### 1. Configure DNS

At your domain registrar, add one of:

**Option A — Apex domain (seinarts.dev):**
```
A     @    185.199.108.153
A     @    185.199.109.153
A     @    185.199.110.153
A     @    185.199.111.153
```

**Option B — Subdomain (docs.seinarts.dev):**
```
CNAME   docs   YOUR_USERNAME.github.io.
```

### 2. Configure GitHub

1. Go to **Settings > Pages**
2. Under **Custom domain**, enter your domain (e.g., `docs.seinarts.dev`)
3. Check **Enforce HTTPS** (wait for certificate provisioning, ~15 minutes)

### 3. Update CNAME File

The `docs/CNAME` file is already set to `docs.seinarts.dev`. Update it to match your actual domain:

```
docs/docs/CNAME
```

### 4. Update Site Config

In `mkdocs.yml`, update these fields:

```yaml
site_url: https://docs.seinarts.dev    # Your custom domain
repo_url: https://github.com/YOUR_USERNAME/SeinARTSFramework
repo_name: SeinARTSFramework
```

Also update the GitHub link in `docs/overrides/main.html`.

## Customization

### Theme Colors

Edit `docs/stylesheets/custom.css`. The color palette is defined in CSS custom properties at the top:

```css
:root {
  --sein-gold:        #D4A843;   /* Primary brand color */
  --sein-blue:        #4A9ED6;   /* Accent / links */
  --sein-surface:     #1A1D23;   /* Dark background */
}
```

### Adding Pages

1. Create a `.md` file in the appropriate `docs/` subdirectory
2. Add the page to the `nav:` section in `mkdocs.yml`
3. Preview locally with `mkdocs serve`

### Logo

Replace the Material icon with a custom logo by adding to `mkdocs.yml`:

```yaml
theme:
  logo: assets/logo.png      # Place in docs/docs/assets/
  favicon: assets/favicon.png
```

## Project Structure

```
docs/
├── mkdocs.yml                    # Site configuration
├── requirements.txt              # Python dependencies
├── DEPLOY.md                     # This file
├── .github/
│   └── workflows/
│       └── deploy-docs.yml       # GitHub Actions workflow
└── docs/
    ├── index.md                  # Home page
    ├── CNAME                     # Custom domain
    ├── stylesheets/
    │   └── custom.css            # Theme overrides
    ├── overrides/
    │   └── main.html             # Template overrides
    ├── getting-started/
    │   ├── index.md
    │   ├── installation.md
    │   └── architecture.md
    ├── guides/
    │   ├── ui-toolkit.md
    │   ├── unit-info-panel.md
    │   └── selection.md
    ├── concepts/
    │   ├── sim-render.md
    │   ├── entities.md
    │   ├── abilities.md
    │   └── fixed-point.md
    └── reference/
        ├── index.md
        ├── ui-toolkit.md
        ├── entity.md
        ├── attributes.md
        ├── abilities.md
        ├── production.md
        └── navigation.md
```
