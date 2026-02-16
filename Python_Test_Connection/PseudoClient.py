import asyncio
import sys
import logging
import os
from amqtt.client import MQTTClient
from amqtt.mqtt.constants import QOS_1

# ================= 配置区域 =================
# 设备唯一标识
DEVICE_ID = "test_device_2"

# 服务器域名 (必须与证书中的 CN 一致，例如 aelecti.top)
# 如果没有 DNS，请在 hosts 文件中映射此域名到服务器 IP
SERVER_HOST = "aelecti.top"
SERVER_PORT = 8883

# CA 证书路径 (用于验证服务器，即服务器端的 .pem 文件)
# 真实设备中这通常是烧录在固件里的 CA 根证书
CA_CERT_FILE = "aelecti.top.pem"

# 构造连接 URI
BROKER_URL = f"mqtts://{SERVER_HOST}:{SERVER_PORT}"
# ===========================================

# 配置日志
logging.basicConfig(level=logging.INFO, format='%(asctime)s - [Device] - %(message)s')
logger = logging.getLogger("ExternalDevice")

# Windows 平台 asyncio 策略修复
if sys.platform == 'win32':
   asyncio.set_event_loop_policy(asyncio.WindowsSelectorEventLoopPolicy())


class SimulatedIoTDevice:
   def __init__(self, deviceId: str, brokerUrl: str, caFile: str):
      self.deviceId = deviceId
      self.brokerUrl = brokerUrl
      self.caFile = caFile

      # 配置 AMQTT 客户端
      # auto_reconnect: 断线自动重连
      self.client = MQTTClient(config={
         'keep_alive': 60,
         'auto_reconnect': True,
         'default_qos': 1,
         # [新增] 配置遗嘱消息 (Last Will)
         # 这样当客户端非正常断开（如脚本被 Kill）时，Broker 会自动向该 Topic 发布 'offline'
         'will': {
            'topic': f'registry/{self.deviceId}/status',
            'message': 'offline',
            'qos': 1,
            'retain': False
         }
      })
      self.isConnected = False

   async def start(self):
      # 检查证书文件是否存在
      if not os.path.exists(self.caFile):
         logger.error(f"CA Certificate file not found: {self.caFile}")
         logger.error("Please copy 'aelecti.top.pem' from server to this directory.")
         return

      try:
         logger.info(f"Connecting to {self.brokerUrl} (SSL)...")
         # 连接到 MQTTS 端口，传入 ca_certs 用于验证服务器
         await self.client.connect(self.brokerUrl)
         self.isConnected = True
         logger.info("✅ Secure connection established!")

         # 1. 上线注册
         await self.registerStatus("online")

         # 2. 订阅控制指令
         cmdTopic = f"device/{self.deviceId}/cmd"
         await self.client.subscribe([(cmdTopic, QOS_1)])
         logger.info(f"Listening for commands on: {cmdTopic}")

         # 3. 启动并发任务：接收消息 & 键盘输入
         await asyncio.gather(
            self.receiveLoop(),
            self.inputLoop()
         )

      except Exception as e:
         logger.error(f"❌ Connection failed: {e}")
         logger.error("Hint: Ensure 'aelecti.top' resolves to Server IP and Certificate matches.")
      finally:
         if self.isConnected:
            # 尝试发送离线消息（如果连接还在）
            try:
               await self.registerStatus("offline")
               await self.client.disconnect()
            except:
               pass

   async def registerStatus(self, status: str):
      topic = f"registry/{self.deviceId}/status"
      await self.client.publish(topic, status.encode(), qos=QOS_1)
      logger.info(f"Status reported: {status}")

   async def receiveLoop(self):
      """接收来自服务器的消息"""
      while self.isConnected:
         try:
            message = await self.client.deliver_message()
            packet = message.publish_packet
            topic = packet.variable_header.topic_name
            payload = packet.payload.data.decode('utf-8')

            logger.info(f"\n📩 [CMD RECV] Topic: {topic} | Content: {payload}")
            print(f">>> ", end="", flush=True)  # 恢复输入提示符

         except Exception as e:
            logger.error(f"Receive loop error: {e}")
            break

   async def inputLoop(self):
      """模拟传感器/用户输入"""
      logger.info("⌨️  Input simulation started. Type message and press ENTER.")
      loop = asyncio.get_running_loop()

      print(f">>> ", end="", flush=True)
      while self.isConnected:
         try:
            # 异步读取键盘输入，不阻塞心跳
            line = await loop.run_in_executor(None, sys.stdin.readline)
            text = line.strip()

            if not text:
               print(f">>> ", end="", flush=True)
               continue

            # 发送消息到 msg Topic
            topic = f"device/{self.deviceId}/msg"
            await self.client.publish(topic, text.encode(), qos=QOS_1)
            logger.info(f"📤 [SENT] {text}")
            print(f">>> ", end="", flush=True)

         except (EOFError, KeyboardInterrupt):
            logger.info("Stopping input loop...")
            break
         except Exception as e:
            logger.error(f"Input error: {e}")


if __name__ == "__main__":
   device = SimulatedIoTDevice(DEVICE_ID, BROKER_URL, CA_CERT_FILE)
   try:
      asyncio.run(device.start())
   except KeyboardInterrupt:
      logger.info("Exiting...")
