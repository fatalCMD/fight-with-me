// Compatibility entry point.
const native=require('./check-native-ljodhs.cjs');
if(require.main===module)native.check();
module.exports=native;
