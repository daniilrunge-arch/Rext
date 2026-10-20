import socket,struct,subprocess,tempfile,json,time,threading,sys,pathlib,shutil,queue
root=pathlib.Path(sys.argv[1]).resolve(); binary=root/'dimensions'
name_mode=sys.argv[2] if len(sys.argv)>2 else "rewrite"
shutdown_mode=sys.argv[3] if len(sys.argv)>3 else "exit"
def port():
 s=socket.socket();s.bind(('127.0.0.1',0));p=s.getsockname()[1];s.close();return p
def packet(i,data=b''):return struct.pack('<HB',len(data)+3,i)+data
def receive(s):
 def exact(n):
  b=b''
  while len(b)<n:
   t=s.recv(n-len(b))
   if not t:raise EOFError
   b+=t
  return b
 return exact(struct.unpack('<H',exact(2))[0]-2)
def cstr(s):b=s.encode();assert len(b)<128;return bytes([len(b)])+b
servers=[];connections=[[],[]];received=[[],[]]
for index in range(2):
 l=socket.socket();l.bind(('127.0.0.1',0));l.listen();servers.append(l)
 def runconn(s,index):
  try:
   s.settimeout(15)
   while True:
    b=receive(s);received[index].append(b)
    if b[:3]==b'\x43\0\0':s.sendall(packet(67,b'\x58\x52'+b[3:]))
    if b[0]==1:s.sendall(packet(3,bytes([index+1])))
    if b[0]==6:s.sendall(packet(7,b'\0'*10+struct.pack('<hh',100,200)+b'\0'*80))
    if b[0]==8:time.sleep(.05);s.sendall(packet(50,bytes([index+1])+b'\0\0')+packet(49))
  except (OSError,EOFError):pass
 def serve(l=l,index=index):
  while True:
   try:s,_=l.accept()
   except OSError:return
   connections[index].append(s);threading.Thread(target=runconn,args=(s,index),daemon=True).start()
 threading.Thread(target=serve,daemon=True).start()
with tempfile.TemporaryDirectory() as d:
 cfg={'listenPort':port(),'restPort':port(),'packetLogging':False,'serverOfflineMessage':'Измерение недоступно.','antiSpamEnabled':False,'requireWarpRule':False,'servers':[{'name':'Lobby','host':'127.0.0.1','port':servers[0].getsockname()[1]},{'name':'Survival','host':'127.0.0.1','port':servers[1].getsockname()[1]},{'name':'Offline','host':'127.0.0.1','port':port()}],'debuffOnSwitch':True,'debuffIds':[163,47,149],'debuffSeconds':5}
 extra_port=port()
 yaml_text=f"""servers:
  - listenPort: {cfg['listenPort']}
    routingServers:
      - name: Lobby
        serverIP: 127.0.0.1
        serverPort: {servers[0].getsockname()[1]}
        isVanilla: false
      - name: Survival
        serverIP: 127.0.0.1
        serverPort: {servers[1].getsockname()[1]}
        isVanilla: false
  - listenPort: {extra_port}
    routingServers:
      - name: Mirror
        serverIP: 127.0.0.1
        serverPort: {servers[1].getsockname()[1]}
        isVanilla: true
        hidden: true
  - routingServers:
      - name: Offline
        serverIP: 127.0.0.1
        serverPort: {cfg['servers'][2]['port']}
options:
  nameChanges:
    mode: {name_mode}
    exclusions: []
  log:
    format:
      file: JSON
  rext:
    antiSpamEnabled: false
  socketTimeout: 0
  restApi:
    enabled: true
    port: {cfg["restPort"]}
    response:
      terrariaServerPort: 45678
      hasServerPassword: true
  tcpRtt:
    enabled: true
    exportToServers: true
    sampleIntervalMs: 100
  blockInvis: true
  language: russian
  languageOverrides:
    noServersAvailable: Измерение недоступно.
  debuffOnSwitch:
    enabled: true
    buffTypes: [163, 47, 149]
    debuffTimeInSeconds: 5
"""
 pathlib.Path(d,'config.yaml').write_text(yaml_text)
 pathlib.Path(d,'extensions').mkdir();shutil.copy(root/'extensions/PlayTime.so',pathlib.Path(d,'extensions/PlayTime.so'))
 log=open(pathlib.Path(d,'output.log'),'w+')
 proc=subprocess.Popen([str(binary)],cwd=d,stdin=subprocess.PIPE,stdout=log,stderr=log,text=True)
 def cmd(s):proc.stdin.write(s+'\n');proc.stdin.flush();time.sleep(.15)
 try:
  for _ in range(60):
   try:c=socket.create_connection(('127.0.0.1',cfg['listenPort']),.2);break
   except OSError:time.sleep(.05)
  c.settimeout(6);seen=[]
  import urllib.request
  with urllib.request.urlopen('http://127.0.0.1:'+str(cfg['restPort'])+'/v2/status',timeout=3) as response:
   status=json.load(response);assert status['port']==45678 and status['serverpassword'] is True

  def until(predicate):
   for _ in range(1000):
    b=receive(c);seen.append(b)
    if predicate(b):return b
   raise AssertionError('packet limit')
  def text(t):return until(lambda b:t.encode() in b)
  def chat(t):c.sendall(packet(82,b'\x01\x00'+cstr('Say')+cstr(t)))
  until(lambda b:b[0]==3)
  c.sendall(packet(1,cstr('Terraria326')));c.sendall(packet(4,b'\x01'+b'\0'*7+cstr('Nene')))
  # Strict backend: never accept packet 50 before spawn (12).
  c.sendall(packet(50,b'\x01'+struct.pack('<HHHH',40,10,54,0)))
  time.sleep(.1);assert not any(b[0]==50 for b in received[0])
  c.sendall(packet(12,b'\x01'+b'\0'*14));time.sleep(.1)
  assert next(i for i,b in enumerate(received[0]) if b[0]==12)<next(i for i,b in enumerate(received[0]) if b[0]==50)
  assert any(b[:3]==b'\x43\x00\x00' and b'127.0.0.1' in b for b in received[0]), 'RealIP missing'
  # Rewritten names keep remaining player appearance bytes intact.
  renamed=packet(4,b'\x01'+b'\0'*7+cstr('Changed')+b'\x09\x08')
  c.sendall(renamed);time.sleep(.1)
  last=next(b for b in reversed(received[0]) if b[0]==4)
  assert (b'Nene' in last) if name_mode=='rewrite' else (b'Changed' in last)
  assert last.endswith(b'\x09\x08')
  connections[0][-1].sendall(renamed)
  shown=until(lambda b:b[0]==4)
  assert (b'Nene' in shown) if name_mode=='rewrite' else (b'Changed' in shown)
  assert sum(b[:3]==b'\x43\x00\x00' for b in received[0])>=3, 'RealIP not refreshed after character/spawn'
  # Extra listener and least-loaded initial route, without a fake probe connection.
  for ingress in [extra_port, cfg['listenPort']]:
   extra=socket.create_connection(('127.0.0.1',ingress),2);extra.settimeout(3)
   assert receive(extra)==b'\x03\x02', 'wrong pool or balancing choice'
   extra.close();time.sleep(.15)
  chat('/dim');line=text('Survival');assert b'Mirror' not in line
  # Client cannot request a backend-only routing operation.
  c.sendall(packet(67,b'\x02\x00'+cstr('Survival')));time.sleep(.15)
  assert not any(b[0]==67 and b[1:3]==b'\x02\x00' for b in received[0])
  assert any(b[:3]==b'\x43\x06\x00' for b in received[0]), 'RTT export missing'
  # Old-world entities: NPC generation, pylon, and a projectile/pet.
  npc=bytearray(27);npc[0]=23;npc[1]=7;npc[2]=9;npc[25]=1;npc[26]=30
  connections[0][-1].sendall(packet(23,bytes(npc[1:]))+packet(82,b'\x08\x00\x00'+struct.pack('<hhB',120,230,2))+packet(27,struct.pack('<IffffhB',1234,1,2,0,0,100,0)))
  until(lambda b:b[0]==27)
  chat('/play Nene');text('сейчас на сервере!')
  cmd('plugins disable PlayTime');cmd('plugins');cmd('plugins enable PlayTime');cmd('hud');cmd('help')
  chat('/dim');text('Survival');chat('/dimensions');text('Survival')
  chat('/ping');ping=text('Ваш пинг в сети');assert b'[i:4085][c/8efa8e:' in ping
  cmd('ip Nene')
  chat('/offline');text('недоступно')
  assert len([b for b in received[0] if b[0]==1])==1
  seen.clear();connections[0][-1].sendall(packet(67,b'\x02\x00'+cstr('Survival')));until(lambda b:b[0]==154)
  buffs=[b for b in seen if b[0]==55];assert [struct.unpack('<H',b[2:4])[0] for b in buffs]==[163,47,149]
  assert seen.index(buffs[0])<next(i for i,b in enumerate(seen) if b[0]==3)
  deadline=time.monotonic()+2
  while not any(b[0]==50 for b in received[1]) and time.monotonic()<deadline:time.sleep(.01)
  assert len([b for b in received[1] if b[0]==12])==1
  ids=[b[0] for b in received[1]];assert ids.index(12)<ids.index(50)
  restored=next(b for b in received[1] if b[0]==50);assert restored[2:]==b'\0\0', 'destination buff state overridden by previous server'
  assert any(b[0]==23 and b[1]==7 and b[2]==9 and b[-1]==0 for b in seen)
  assert any(b[0]==82 and b[1:4]==b'\x08\x00\x01' for b in seen)
  assert any(b[0]==29 and struct.unpack('<I',b[1:5])[0]==1234 for b in seen)
  assert len([b for b in seen if b[0]==55])==3
  assert not any('Вы перемещены'.encode() in b for b in seen)
  spawn=next(b for b in received[1] if b[0]==12);assert struct.unpack('<hh',spawn[2:6])==(100,200) and spawn[15]==1
  # Periodic WorldInfo must still reach the client after READY, without respawn.
  seen.clear()
  for x in [100,100,101,101]:
   world=b'\0'*10+struct.pack('<hh',x,200)+b'\0'*80
   connections[1][-1].sendall(packet(7,world))
   assert until(lambda b:b[0]==7)==bytes([7])+world
  assert not any(b[0] in (3,12,49,55) for b in seen)
  connections[1][-1].sendall(packet(2,b'\0'+cstr('Misbehaviour.')));text('Misbehaviour.');until(lambda b:b[0]==154)
  chat('/void');void=text('Вы вошли в Пустоту');assert b'[c/f88efa:' in void
  before=sum(b[0]==17 for b in received[0]);c.sendall(packet(17,b'\x01'*10));time.sleep(.1);assert sum(b[0]==17 for b in received[0])==before
  chat('/dim');text('Survival');chat('/back');until(lambda b:b[0]==154)
  cmd('warps add Lobby Survival 100 100 2 2');cmd('warps list');assert pathlib.Path(d,'warps.db').exists()
  def move(x,y):c.sendall(packet(13,b'\x01'+b'\0'*5+struct.pack('<ff',x*16,y*16)))
  move(100,100);text('через 5');move(90,90);text('Телепортация отменена')
  cmd('ban Nene test');until(lambda b:b[0]==2);assert 'name:nene' in pathlib.Path(d,'bans.db').read_text()
  cmd('unban Nene')
  ending=socket.create_connection(('127.0.0.1',cfg['listenPort']),2);ending.settimeout(5)
  assert receive(ending)[0]==3
  idle_http=socket.create_connection(('127.0.0.1',cfg['restPort']),2)
  if shutdown_mode=='exit':cmd('exit')
  else:
   import signal
   proc.send_signal(signal.SIGINT if shutdown_mode=='sigint' else signal.SIGTERM)
  bye=receive(ending);assert bye[0]==2 and 'Сервер перезагружается'.encode() in bye
  ending.close();proc.wait(timeout=5);assert proc.returncode==0
  idle_http.close()
  for logpath in pathlib.Path(d,'logs').glob('dimensions-*.log'):
   for line in logpath.read_text().splitlines():
    if line.startswith('{'):assert 'message' in json.loads(line)

  log.seek(0);out=log.read();assert 'PlayTime v1.0.0 loaded' in out and '[OFF] PlayTime' in out
  assert '[REALIP] Applied:' in out and 'applied=YES' in out
  world_lines=[line[line.index('[WORLD]'):] for line in out.splitlines() if '[WORLD]' in line]
  assert world_lines and len(world_lines)==len(set(world_lines)), 'repeated WorldInfo console log'
  assert '[SPAWN] WorldInfo spawn' not in out
  print('PASS YAML/bridge/multi-listener/balancing/hidden/blockInvis/RealIP/RTT:  strict buff ordering + pet buff preservation + NPC/pylon/projectile cleanup + kick reason;  extension load/command/toggle; dim/dimensions; native RTT; offline; alias switch; early buffs; spawn; kickback; void/back; persisted warp + cancellation; ban/unban; shutdown')
 finally:
  if proc.poll() is None:proc.kill()
  if proc.returncode != 0:log.seek(0);print(log.read())
