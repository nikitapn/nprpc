const path = require('path');

module.exports = {
  mode: 'development',
  target: 'web',
  entry: './test/browser/webtransport-runtime.ts',
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
      nprpc$: path.resolve(__dirname, '../../nprpc_js/dist/index.esm.js'),
    },
  },
  output: {
    filename: 'webtransport-test-runtime.js',
    path: path.resolve(__dirname, 'dist/browser'),
    library: 'nprpc_test_runtime',
    libraryTarget: 'umd',
    umdNamedDefine: true,
    globalObject: 'typeof self !== "undefined" ? self : this',
  },
};