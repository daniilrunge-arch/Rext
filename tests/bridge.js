const assert=require('assert');
const {pktDimensionsUpdate,handleDimensionsPacket}=require('../integrations/tls-warpplates/dimensions-packets');
assert.equal(pktDimensionsUpdate('survival').toString('hex'),'0e0043020008737572766976616c');
const ip=Buffer.from('127.0.0.1'),f=Buffer.alloc(6+ip.length);f.writeUInt16LE(f.length);f[2]=67;f[5]=ip.length;ip.copy(f,6);
let ack;const c={send:frame=>{ack=frame;}};assert.throws(()=>handleDimensionsPacket(c,f,false));assert(handleDimensionsPacket(c,f,true));assert.equal(c._realIpFromDimensions,'127.0.0.1');assert.equal(c.ip,'127.0.0.1');assert.equal(ack.readUInt16LE(3),0x5258);
const r=Buffer.alloc(26);r.writeUInt16LE(26);r[2]=67;r[3]=6;r[5]=3;r.writeInt32LE(1500,6);r.writeInt32LE(-1,10);r.writeInt32LE(1500,14);r.writeBigUInt64LE(123n,18);
assert(handleDimensionsPacket(c,r,true));assert.equal(c.dimensionsRtt.clientRttMs,1.5);
const long=pktDimensionsUpdate('x'.repeat(130));assert.equal(long[5],130);assert.equal(long[6],1);
console.log('PASS: independent warp packet builder; trusted RealIP; client RTT decoder');
