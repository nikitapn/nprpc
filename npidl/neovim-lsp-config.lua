-- Neovim LSP configuration for npidl
-- Add this to your Neovim config (e.g., ~/.config/nvim/lua/lsp-npidl.lua)
-- Then require it in your init.lua: require('lsp-npidl')

local lspconfig = require('lspconfig')
local configs = require('lspconfig.configs')

-- Define npidl LSP server configuration
if not configs.npidl then
  configs.npidl = {
    default_config = {
      cmd = { '/path/to/npsystem/build/linux/bin/npidl', '--lsp' },
      filetypes = { 'npidl' },
      root_dir = function(fname)
        return lspconfig.util.find_git_ancestor(fname) or vim.fn.getcwd()
      end,
      settings = {},
    },
  }
end

-- Start npidl LSP server for .npidl files
lspconfig.npidl.setup{
  on_attach = function(client, bufnr)
    -- Enable completion triggered by <c-x><c-o>
    vim.api.nvim_buf_set_option(bufnr, 'omnifunc', 'v:lua.vim.lsp.omnifunc')

    -- Mappings
    local bufopts = { noremap=true, silent=true, buffer=bufnr }
    vim.keymap.set('n', 'gD', vim.lsp.buf.declaration, bufopts)
    vim.keymap.set('n', 'gd', vim.lsp.buf.definition, bufopts)
    vim.keymap.set('n', 'K', vim.lsp.buf.hover, bufopts)
    vim.keymap.set('n', 'gi', vim.lsp.buf.implementation, bufopts)
    vim.keymap.set('n', '<C-k>', vim.lsp.buf.signature_help, bufopts)
    vim.keymap.set('n', 'gr', vim.lsp.buf.references, bufopts)
    vim.keymap.set('n', '<leader>rn', vim.lsp.buf.rename, bufopts)
    vim.keymap.set('n', '<leader>ca', vim.lsp.buf.code_action, bufopts)
    vim.keymap.set('n', '<leader>f', function() vim.lsp.buf.format { async = true } end, bufopts)
  end,
  flags = {
    debounce_text_changes = 150,
  }
}

-- Set up .npidl filetype detection
vim.filetype.add({
  extension = {
    npidl = 'npidl',
  },
})
