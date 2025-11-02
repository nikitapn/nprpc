;; Emacs LSP configuration for npidl
;; Add this to your Emacs config (e.g., ~/.emacs.d/init.el or ~/.emacs)
;; Requires lsp-mode to be installed

(require 'lsp-mode)

;; Define npidl major mode
(define-derived-mode npidl-mode prog-mode "npidl"
  "Major mode for editing .npidl files."
  (setq-local comment-start "//")
  (setq-local comment-end ""))

;; Associate .npidl files with npidl-mode
(add-to-list 'auto-mode-alist '("\\.npidl\\'" . npidl-mode))

;; Register npidl LSP server
(lsp-register-client
 (make-lsp-client
  :new-connection (lsp-stdio-connection '("/path/to/npsystem/build/linux/bin/npidl" "--lsp"))
  :activation-fn (lsp-activate-on "npidl")
  :server-id 'npidl))

;; Enable LSP for npidl files
(add-hook 'npidl-mode-hook #'lsp)

;; Optional: Enable flycheck for on-the-fly diagnostics
(add-hook 'npidl-mode-hook #'flycheck-mode)
