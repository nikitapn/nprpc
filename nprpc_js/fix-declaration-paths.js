const fs = require('fs');
const path = require('path');

class FixDeclarationPathsPlugin {
  constructor(options = {}) {
    this.options = {
      src: options.src || 'src',
      dist: options.dist || 'dist',
      ...options
    };
  }

  apply(compiler) {
    compiler.hooks.afterEmit.tap('FixDeclarationPathsPlugin', (compilation) => {
      const distDir = path.join(compiler.options.context, this.options.dist);
      this.processDirectory(distDir, distDir);
    });
  }

  processDirectory(dir, rootDir) {
    if (!fs.existsSync(dir)) {
      return;
    }

    const items = fs.readdirSync(dir);

    items.forEach(item => {
      const fullPath = path.join(dir, item);
      const stat = fs.statSync(fullPath);

      if (stat.isDirectory()) {
        this.processDirectory(fullPath, rootDir);
      } else if (item.endsWith('.d.ts')) {
        this.fixDeclarationFile(fullPath, rootDir);
      }
    });
  }

  fixDeclarationFile(filePath, rootDir) {
    try {
      let content = fs.readFileSync(filePath, 'utf8');
      let modified = false;

      // Replace @/ aliases with relative paths
      content = content.replace(/(import|export)([^'"]*)(['"])@\/([^'"]+)(['"])/g, (match, importExport, beforePath, openQuote, importPath, closeQuote) => {
        const currentDir = path.dirname(filePath);
        const targetPath = path.join(rootDir, importPath);
        const relativePath = path.relative(currentDir, targetPath);

        // Normalize path separators for cross-platform compatibility
        const normalizedPath = relativePath.replace(/\\/g, '/');

        // Ensure relative paths start with './' or '../'
        const finalPath = normalizedPath.startsWith('.') ? normalizedPath : `./${normalizedPath}`;

        modified = true;
        return `${importExport}${beforePath}${openQuote}${finalPath}${closeQuote}`;
      });

      // Also handle 'from "@/...' patterns
      content = content.replace(/from\s+['"]@\/([^'"]*)['"]/g, (match, importPath) => {
        const currentDir = path.dirname(filePath);
        const targetPath = path.join(rootDir, importPath);
        const relativePath = path.relative(currentDir, targetPath);

        // Normalize path separators for cross-platform compatibility
        const normalizedPath = relativePath.replace(/\\/g, '/');

        // Ensure relative paths start with './' or '../'
        const finalPath = normalizedPath.startsWith('.') ? normalizedPath : `./${normalizedPath}`;

        modified = true;
        return `from "${finalPath}"`;
      });

      if (modified) {
        fs.writeFileSync(filePath, content, 'utf8');
        console.log(`Fixed path aliases in: ${path.relative(process.cwd(), filePath)}`);
      }
    } catch (error) {
      console.error(`Error fixing declaration file ${filePath}:`, error);
    }
  }
}

module.exports = FixDeclarationPathsPlugin;
