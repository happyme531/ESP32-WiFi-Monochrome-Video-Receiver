#include <TFT_eSPI.h>
#include <WiFi.h>
#include <queue>

using namespace std;

char* ssid     = ""; //填写你的wifi名字
char* password = ""; //填写你的wifi密码

constexpr uint8_t bufFrame = 10;//缓存队列大小

//想要更改图像尺寸只需要修改这里，上位机无需修改
//!!请注意!! 如果图片的 长x宽/8 >1492 那么你需要把wifiClient.cpp中的1492 修改成 长x宽/8 的值 否则图像会严重撕裂！
constexpr uint8_t frameHeight=180,frameWidth=240;

struct frame {
  uint8_t data[frameHeight*frameWidth/8] = {}; //定义缓冲区 
};
//接受数据的缓冲区
queue<frame> frames;

const uint8_t result_msg[1] = {0x00}; //定义返回信息
WiFiClient client; //初始化一个客户端对象
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite rawFrame = TFT_eSprite(&tft);
uint16_t* rawFramePtr = nullptr;
xTaskHandle drawAppleHandle;

//fps计数器
uint32_t startTime, frameCnt = 0;
uint8_t lastFPS;
uint8_t lastBufSize;

extern "C" {
extern uint8_t temprature_sens_read();
};
void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password); //连接wifi
  tft.begin();
  tft.initDMA();
  delay(1000); //等待1秒
  if (WiFi.status() == WL_CONNECTED) //判断如果wifi连接成功
  {
    const int httpPort = 715; //设置上位机端口
    client.connect("10.10.10.111", httpPort); //连接到上位机，ip改成你的局域网ip
    client.write(result_msg, 1); //发送返回信息
  }
  rawFrame.setColorDepth(16);
  rawFramePtr = (uint16_t*)rawFrame.createSprite(frameWidth,frameHeight);
  
  xTaskCreatePinnedToCore(
    drawApple
    ,  "drawApple"   // A name just for humans
    ,  1024 * 9 // This stack size can be checked & adjusted by reading the Stack Highwater
    ,  NULL
    ,  1  // Priority, with 3 (configMAX_PRIORITIES - 1) being the highest, and 0 being the lowest.
    ,  &drawAppleHandle
    ,  0);
  disableCore0WDT();//如果不增加这一行，程序会在运行几秒钟后报错(原因未知)
  disableCore1WDT();
  startTime = millis();
}

void loop() {
  while (client.available()>=frameHeight*frameWidth/8) {   //如果TCP的缓冲区有数据并且数据是完整的
    while (frames.size() >= bufFrame) delay(1);
    struct frame thisFrame;
    memset(&thisFrame.data,0,sizeof(thisFrame.data));
    client.read(thisFrame.data, frameHeight*frameWidth/8);//写入当前接收的帧
    frames.push(thisFrame);//把这一帧放入缓存
    client.write(result_msg, 1); //发送返回信息
  };
}

uint64_t frameTime;
void drawApple( void *pvParameters ){
  while (1) {
    while (frames.empty()) delay(1); //缓存中无数据时等待
    frameTime=millis();
    lastBufSize=frames.size();
    struct frame thisFrame = frames.front();
    drawFrameDMA(thisFrame.data);
    frameCnt++;
    frames.pop();
    while(frameTime+16>millis()); //保证稳定的60fps（其实是62.5)
    if (frameCnt >= 30) { //fps计数
      uint32_t elapsed = (millis() - startTime); 
      Serial.print(((double)frameCnt /elapsed)*1000);
      Serial.println(" fps");
      lastFPS=((double)frameCnt /elapsed)*1000;
      frameCnt = 0;
      startTime = millis();
    };
  };
};

void drawFrameDMA(const uint8_t *bitmap) {
  int32_t i, j, byteWidth = (frameWidth + 7) / 8;
  for (j = 0; j < frameHeight; j++) {
    for (i = 0; i < frameWidth; i++ ) {
      //解码XBitMap
      if (pgm_read_byte(bitmap + j * byteWidth + i / 8) & (1 << (i & 7))) {
          rawFrame.drawPixel(i , j , TFT_WHITE);
      } else {
          rawFrame.drawPixel(i , j, TFT_BLACK);
      };
    };
  };
  rawFrame.setCursor(0,0);
  rawFrame.setTextSize(2);
  rawFrame.setTextFont(1);
  rawFrame.setTextColor(TFT_GREEN);
  rawFrame.printf("FPS%d,Buf%d",lastFPS,lastBufSize);
  //开始DMA传输
  tft.pushImageDMA(0, 240-frameHeight, frameWidth, frameHeight, rawFramePtr);
  //tft.pushImageDMA(0, 240-180+frameHeight/2, frameWidth, frameHeight/2, rawFramePtr+frameWidth*frameHeight/2);
};
