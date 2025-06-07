const path = require('path');
const FixDeclarationPathsPlugin = require('./fix-declaration-paths');

module.exports = env => {
  let production = env.production == 'true' ? true : false;
  let watch = env.watch == 'true' ? true : false;
  return {
    entry: './src/index.ts',
    devtool: production ? undefined : 'source-map',
    watch: watch,
    mode: !production ? 'development' : 'production',
    optimization: {
      minimize: production,
    },
    module: {
      rules: [
        {
          test: /\.ts$/,
          use: 'ts-loader',
          exclude: /node_modules/,
        },
      ],
    },
    resolve: {
      extensions: ['.ts', '.js'],
      alias: {
        '@': path.resolve(__dirname, 'src/'),
      },
    }, 
    plugins: [
        new FixDeclarationPathsPlugin()
    ],
    output: {
      filename: 'index.js',
      libraryTarget: 'umd',
      library: 'nprpc_runtime',
      umdNamedDefine: true,
      path: path.resolve(__dirname, 'dist'),
    },
  }
};