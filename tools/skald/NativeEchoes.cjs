// Compatibility entry point; native particles now cover all four Ljodhs.
const native=require('./NativeLjodhs.cjs');
if(require.main===module)native.build();
module.exports=native;
