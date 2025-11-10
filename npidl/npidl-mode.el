;;; npidl-mode --- NPIDL major mode and LSP configuration

;------------------------------------------------------------------------------

;;; Commentary:
;; Emacs LSP configuration for npidl
;; Add this to your Emacs config (e.g., ~/.emacs.d/init.el or ~/.emacs)
;; Requires lsp-mode to be installed

;------------------------------------------------------------------------------

;;; Code:
;;
;; Written by Nikita

(require 'lsp-mode)

;; Register npidl language
(add-to-list 'lsp-language-id-configuration '(npidl-mode . "npidl"))

;; Register npidl LSP server
(lsp-register-client
 (make-lsp-client
  :new-connection (lsp-stdio-connection '("bash" "-c"
     "/home/nikita/projects/npsystem/build/linux/bin/npidl --lsp 2>/tmp/npidl-stderr.log"))
  :activation-fn (lsp-activate-on "npidl")
  :server-id 'npidl-lsp))

;; Auto-start LSP for npidl files
(add-hook 'npidl-mode-hook #'lsp)

(define-derived-mode npidl-mode prog-mode "NPIDL"
  "Major mode for editing NPIDL interface definition files."
  (setq-local comment-start "// ")
  (setq-local comment-end ""))

;; Associate .npidl files with npidl-mode
(add-to-list 'auto-mode-alist '("\\.npidl\\'" . npidl-mode))

(defvar npidl-font-lock-keywords
  `(
    ;; Keywords
    (,(regexp-opt '("module" "import" "interface" "struct" "exception" "enum"
                    "const" "using" "async" "in" "out" "direct" "raises"
                    "flat" "trusted") 'words)
     . font-lock-keyword-face)

    ;; Types
    (,(regexp-opt '("void" "boolean" "object"
                    "i8" "u8" "i16" "u16" "i32" "u32"
                    "i64" "u64" "f32" "f64"
                    "string" "vector") 'words)
     . font-lock-type-face)

    ;; Attributes
    ("\\[\\([a-zA-Z_][a-zA-Z0-9_]*\\)\\(=[^]]+\\)?\\]"
     (1 font-lock-preprocessor-face))

    ;; Type names (CamelCase identifiers)
    ("\\b[A-Z][a-zA-Z0-9_]*\\b" . font-lock-type-face)

    ;; Function names
    ("\\b[a-zA-Z_][a-zA-Z0-9_]*\\s-*("
     (0 font-lock-function-name-face))
    )
  "Highlighting for NPIDL mode.")

(define-derived-mode npidl-mode prog-mode "NPIDL"
  "Major mode for editing NPIDL interface definition files."
  (setq-local comment-start "// ")
  (setq-local comment-end "")
  (setq font-lock-defaults '(npidl-font-lock-keywords)))

(add-to-list 'auto-mode-alist '("\\.npidl\\'" . npidl-mode))

;; Optional: Enable flycheck for on-the-fly diagnostics
;; (add-hook 'npidl-mode-hook #'flycheck-mode)

; This file provides npidl-mode.
(provide 'npidl-mode)

;;; npidl-mode.el ends here
