# Project-Specific Notes for Agents

## Settings Page URL Management

When updating features that modify the settings page (e.g., adding the routine persistence feature), remember:

### Testing Workflow

1. **URL Configuration:** The settings page URL is defined in `gymtracker/src/pkjs/index.js`:
   ```javascript
   var myConfigUrl = isDevMode ? 'https://oliverano95.github.io/GymTracker/index_dev.html' : 'https://oliverano95.github.io/GymTracker/';
   ```

2. **For local testing:** Change the URL to your GitHub Pages (`username.github.io/repo`), test on CloudPebble
3. **Before PR:** Revert URL back to upstream's URL

### GitHub Pages Branch Configuration

When testing features on GitHub Pages that are on a non-main branch:
- Go to **Settings** → **Pages** → Select branch from dropdown
- This allows GitHub Pages to serve code from feature branches
- Remember to switch back after testing