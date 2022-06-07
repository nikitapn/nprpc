const path = require('path');

module.exports = env => {
	let is_debug = env.debug == 'true' ? true : false;
  return {
	entry: './src/index.ts',
	watch: is_debug,
	mode: is_debug ? 'development' : 'production',
  optimization: {
    minimize: !is_debug,
  },
  module: {
    rules: [
      {
        test: /\.tsx?$/,
        use: 'ts-loader',
        exclude: /node_modules/,
      },
    ],
  },
  resolve: {
    extensions: ['.ts', '.js'],
  },
  output: {
    filename: 'index.js',
		libraryTarget: 'umd',
    library: 'nprpc_runtime',
    umdNamedDefine: true,
    path: path.resolve(__dirname, 'dist'),
  },
  
}
};