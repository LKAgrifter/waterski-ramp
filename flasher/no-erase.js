// NEVER ERASE THE BOARD.
// Erasing the whole flash also deletes the Heltec radio license that each board keeps in flash (in its "app1"
// partition). Without it the firmware stops at start-up with "Please provide a correct license!" and the screen stays
// off. So this page makes ESP Web Tools always install WITHOUT erasing: every install in its dialog goes through
// _startInstall(erase), and here that always gets erase = false. The manifests also set new_install_prompt_erase:
// false, so the "Erase device" question is never shown.
// _startInstall is an internal name of ESP Web Tools 10.1.0 (the version index.html pins). If a different version
// doesn't have it, this refuses to open the installer rather than risk an erase.
(function () {
  'use strict';
  let safe = null;   // null = not checked yet, true = patched, false = can't patch

  customElements.whenDefined('ewt-install-dialog').then(() => {
    const Dialog = customElements.get('ewt-install-dialog');
    const original = Dialog && Dialog.prototype._startInstall;
    if (typeof original !== 'function') { safe = false; return; }
    Dialog.prototype._startInstall = function () { return original.call(this, false); };
    safe = true;
  });

  // Belt and braces: if the patch couldn't be applied, close any installer dialog as soon as it opens
  new MutationObserver(records => {
    for (const r of records) {
      for (const node of r.addedNodes) {
        if (node.nodeName === 'EWT-INSTALL-DIALOG' && safe === false) {
          node.remove();
          alert('This flasher page needs updating (its installer changed), so it will not flash: it could erase the ' +
                "board's Heltec license. Please report it to whoever maintains the ramp firmware.");
        }
      }
    }
  }).observe(document.body, { childList: true });

  window.RampNoErase = { isSafe: () => safe };
})();
