#include <PS2Keyboard.h>

#include <SPI.h>
#include <SD.h>
#include <EthernetV2_0.h>
#include <NMT_GFX.h>
#define ax    regs[0]
#define bx    regs[1]
#define cx    regs[2]
#define dx    regs[3]
#define si    regs[4]
#define di    regs[5]
#define sp    regs[6]
#define pc    regs[7]
#define RAM_LEN 4096
//#define DEBUG
#define SD_DEBUG
byte carry=0;
byte RAM[RAM_LEN];
int32_t regs[8];
byte opcode;
byte mac[] = {  0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
NMT_GFX vga;
PS2Keyboard kbd;
IPAddress ip(192, 168, 0, 177);
EthernetClient client;
IPAddress server(192,168,1,132);
bool has_sd=false;
void setup() {
  // put your setup code here, to run once:
  vga.begin();
  Serial.begin(9600);
  kbd.begin(8,2);
  pinMode(10, OUTPUT);
  digitalWrite(10, HIGH);
  pinMode(4, OUTPUT);
  digitalWrite(4, HIGH);
  if (Ethernet.begin(mac) == 0) {
    Ethernet.begin(mac, ip);
  }
  init_sd();
  vga.println("--BIOS--\nAny key to access bootman");
  delay(3000);
  if(kbd.available()){
    vga.println("BOOT DEVICE SELECT:\n1 : SD CARD\n2 : NETWORK");
    bool iv=true;
    while(iv){
      vga.print(" > ");
      while(kbd.available())
        kbd.read();
      while(!kbd.available());
      char c=kbd.read();
      vga.println(c);
      if(c=='1'){
        iv=!sdboot();
      }else if(c=='2'){
        iv=!netboot();
      }else
        vga.println("Invalid selection.");
    }
  }else{
    if(!sdboot()){
      if(!netboot()){
        bx=1;
        vga.println("No boot source!");
        //while(true);
        RAM[0]=0x0c;
        RAM[1]=18;
        RAM[2]=0;
        RAM[3]=0;
        RAM[4]=0;
        RAM[5]=0x18;
        RAM[6]=0x2c;
        RAM[7]=0x06;
        RAM[8]=0;
        RAM[9]=0;
        RAM[10]=0;
        RAM[11]=0x38;
        RAM[12]=0x01;
        RAM[13]=0x0F;
        RAM[14]=0x05;
        RAM[15]=0;
        RAM[16]=0;
        RAM[17]=0;
        char* str = "This is the NULL program.";
        for(int i=0;str[i]!=0;i++)
          RAM[18+i]=str[i];
      }
    }
  }
  sp=RAM_LEN-1;
  pc=0;
  while(kbd.available())
    kbd.read();
  Serial.println("Executing code...");
}
void loop() {
  opcode=RAM[pc];
  byte macrop=opcode&0b11111000;
  #ifdef DEBUG
    delay(75);
    Serial.print(pc,HEX);
    Serial.print(" : ");
    Serial.println(opcode,HEX);
  Serial.print("  SP : ");
  Serial.println(sp);
  #endif
  pc++;
  if((opcode&0b10001000)==0){  //  mov r, r
    regs[opcode&7]=regs[opcode>>4];
  }else if((opcode&0b10001000)==128){  // (math) ax, r
    ax=math(regs[opcode&7],7&(opcode>>4));
  }else if(macrop==0x98){  //  inc r
    regs[opcode&7]+=1;
  }else if(macrop==0xA8){  //  dec r
    regs[opcode&7]-=1;
    #ifdef DEBUG
    Serial.println("  DEC q");
    #endif
  }else if(macrop==0xB8){  //  not r
    regs[opcode&7]=~regs[opcode&7];
  }else if(macrop==0xC8){  //  neg r
    regs[opcode&7]=-regs[opcode&7];
  }else if(macrop==0xD8){  //  push r
    push(regs[opcode&7]);
  }else if(macrop==0xE8){  //  pop r
    regs[opcode&7]=pop();
  }else if(macrop==0x88){  // (math) ax, *
    ax=math(read(pc, 4),opcode&7);
    pc+=4;
  }else if(macrop==0x08){
    pc+=4;
    regs[opcode&7]=read(pc-4, 4);
  }else if(macrop==0x78){  // ex ax, r
    int32_t tmpe=regs[opcode&7];
    regs[opcode&7]=ax;
    ax=tmpe;
  }else if(opcode==0x2F){  // ex si, di
    int32_t tmpe=si;
    si=di;
    di=tmpe;
  }else if((opcode&0b11111100)==0x18){
    ax=read(si,pow(2,opcode&3));
    si+=pow(2,opcode&3);
  }else if((opcode&0b11111100)==0x1C){
    write(di,pow(2,opcode&3),ax&255);
    di+=pow(2,opcode&3);
  }else if(opcode==0x38){
    out(ax, read(pc, 1));
    pc++;
  }else if(opcode==0x39){
    out(ax, bx);
  }else if(opcode==0x3A){
    ax=in(read(pc, 1));
    pc++;
  }else if(opcode==0x3B){
    ax=in(bx);
  }else if(opcode==0x2C){
    #ifdef DEBUG
      Serial.print("  JPZ : AX=");
      Serial.println(ax,HEX);
    #endif
    if(ax==0){
      pc=read(pc,4);
    }else
      pc+=4;
  }else if(opcode==0x2D){ // call *
    push(pc+4);
    pc=read(pc,4);
  }else if(opcode==0x3E){ // rtz
    if(ax==0){
      pc=pop();
    }
  }else if(opcode==0x3D){ // call ax
    push(pc-1);
    pc=ax;
  }else if(opcode==0x2E){ // ret
    pc=pop();
  }else if(opcode==0x3C){
    if(ax!=0){
      pc=read(pc,4);
    }else
      pc+=4;
  }else{
    vga.print("BAD OPCODE: ");
    vga.println(String(opcode,HEX).c_str());
    vga.print("ADDRESS     ");
    vga.println(String(pc,HEX).c_str());
    vga.print("SP          ");
    vga.println(String(sp,HEX).c_str());
    vga.print("Any key to reboot");
    while(kbd.available())  // clear keyboard buffer
      kbd.read();
    while(!kbd.available());// wait for key
    kbd.read();
    setup();
  }
  // All done!
}
void init_sd(){
  has_sd=SD.begin(4);
}
bool netboot(){
  vga.println("connecting to 192.168.1.132...");
  byte error=false;
  client=EthernetClient();// just to be sure
  if (!client.connect(server,55555)){
    vga.println("Cannot access server. Network cannot boot.");
    return false;
  }else{
    client.println("GET /boot.bin HTTP/1.1");
    client.println();
  }
  delay(700);
  vga.println("Downloading bootcode...");
  pc=0;
  while((client.connected()||client.available())&&pc<512){
    while(client.available()){
      RAM[pc]=(byte)client.read();
      #ifdef DEBUG
        Serial.print(pc);
        Serial.print(" : ");
        Serial.println(RAM[pc]);
      #endif
      pc++;
    }
  }
  return true;
}
bool sdboot(){
  if(has_sd&&SD.exists("boot.bin")){
    vga.println("Booting from SD card...");
    File boot=SD.open("boot.bin");
    pc=0;
    while(boot.available()&&pc<512){
      RAM[pc]=boot.read();
      #ifdef DEBUG
        vga.print(pc);
        vga.print(" : ");
        vga.println(RAM[pc]);
      #endif
      pc++;
    }
    boot.close();
    return true;
  }else{
    vga.println("SD card couldn't boot.");
    return false;
  }
}
void push(int32_t i){
  sp-=4;
  write(sp, 4, i);
}
int32_t pop(){
  sp+=4;
  return read(sp-4,4);
}
int32_t math(uint32_t arg, byte op){
  #ifdef DEBUG
    Serial.print("  MATH : ");
    Serial.println(op);
  #endif
  switch (op) {
    case 0: return ax+arg;
    case 1: return ax-arg;
    case 2: return ax*arg;
    case 3: return ax/arg;
    case 4: return ax|arg;
    case 5: return ax&arg;
    case 6: return ax^arg;
  }
  return -1;
}
int32_t read(uint32_t adr,byte len){
  int32_t o=0;
  for(byte i=0;i<len;i++){
    o|=((int32_t)RAM[i+adr])<<(8*i);
  }
  #ifdef DEBUG
    Serial.println("  READING");
    Serial.print("    ADR : ");
    Serial.println(adr, HEX);
    Serial.print("    LEN : ");
    Serial.println(len, HEX);
    Serial.print("    DAT : ");
    Serial.println(o, HEX);
  #endif
  return o;
}
int32_t write(uint32_t adr, byte len, byte data){
  int32_t o=0;
  len--;
  for(byte i=0;i<=len;i++){
    RAM[adr]=(data>>(8*(len-i)))&255;
  }
  return o;
}
byte gkbd(){
  byte tmp=kbd.read();
  if(tmp=='\n'||tmp=='\r')
    tmp=13;
  return tmp;
}
File usr_file;
byte fnamenloc=0;
char fnamen[16];
void out(byte d, byte a){
  if(a==1){        // WR_TERM
    if(d==8)
      Serial.println("VGA_BACKSPACE");
    if(d>7)
      vga.print((char)d);
    else
      Serial.println("  BAD OUT 0x01: "+String(d,HEX));
  }else if(a==2){  // WR_FILE_NAME
    fnamen[fnamenloc]=d;
    fnamenloc++;
    if((fnamenloc>=16)||(d==0))
      fnamenloc=0;
    #ifdef SD_DEBUG
    #endif
  }else if(a==3){  // OPEN_FILE
    if(usr_file)
      usr_file.close();
    usr_file=SD.open(fnamen, FILE_WRITE|FILE_READ);
    #ifdef SD_DEBUG
    Serial.print("OPEN: ");
    Serial.println(fnamen);
    #endif
  }else if(a==4){  // CLOSE_FILE
    usr_file.close();
    usr_file;
    #ifdef SD_DEBUG
    Serial.println("CLOSE;");
    #endif
  }else if(a==5){  // WR_FILE
    usr_file.write(d);
    #ifdef SD_DEBUG
    #endif
  }else if(a==6){  // DELETE_FILE
    SD.remove(fnamen);
    #ifdef SD_DEBUG
    Serial.print("DELETE: ");
    Serial.println(fnamen);
    #endif
  }
}
byte in(byte a){
  if(a==1){
    if(kbd.available()){
      return gkbd();
    }else
      return 0;
  }else if(a==2){
    return SD.exists(fnamen);
  }else if(a==3){
    return usr_file.available();
  }else if(a==4){
    return usr_file.read();
  }
  return 0;
}
void returnf(){
  #ifdef DEBUG
  Serial.print("  SP before RET : ");
  Serial.println(sp);
  #endif
  pc=pop();
  #ifdef DEBUG
  Serial.print("  New PC after RET : ");
  Serial.println(pc);
  #endif
}
int freeMemory () {
  extern int __heap_start, *__brkval; 
  int v; 
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval); 
}
void printRam(int x){
  /*
  for(int i=0;i<x;i++){
    vga.print(i,HEX);
    vga.print(" : ");
    vga.println(RAM[i],HEX);
  }*/
}
