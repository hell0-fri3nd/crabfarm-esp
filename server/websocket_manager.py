import json
from typing import Set, Optional
import logging
import asyncio

logger = logging.getLogger(__name__)

class WSManager:
    def __init__(self):
        self.esp32: Optional = None
        self.web_clients: Set = set()
        self.esp32_connected = False
        self.last_heartbeat = 0
    
    async def broadcast(self, message: dict):
        if not self.web_clients:
            return
        disconnected = set()
        for client in self.web_clients:
            try:
                await client.send_json(message)
            except Exception as e:
                logger.warning(f"Failed to send to web client: {e}")
                disconnected.add(client)
        for client in disconnected:
            self.web_clients.remove(client)
    
    async def send_to_esp32(self, message: str) -> bool:
        """Send a command to ESP32"""
        if self.esp32 is not None and self.esp32_connected:
            try:
                # Ensure message is string
                if isinstance(message, dict):
                    message = json.dumps(message)
                await self.esp32.send_text(message)
                logger.info(f"📤 Sent to ESP32: {message}")
                return True
            except Exception as e:
                logger.error(f"Failed to send to ESP32: {e}")
                self.esp32_connected = False
                self.esp32 = None  # Clear the reference
                return False
        else:
            logger.warning(f"⚠️ Cannot send to ESP32: Not connected (connected={self.esp32_connected}, esp32={self.esp32 is not None})")
            # Broadcast to web clients that ESP32 is disconnected
            await self.broadcast({
                'type': 'esp32_status',
                'connected': False
            })
            return False
    
    async def cancel_test(self) -> bool:
        """Send cancel command to ESP32"""
        if self.esp32 and self.esp32_connected:
            try:
                await self.esp32.send_text("CANCEL_TEST")
                logger.info("📤 Sent CANCEL_TEST to ESP32")
                return True
            except Exception as e:
                logger.error(f"Failed to send cancel to ESP32: {e}")
                return False
        else:
            logger.warning("⚠️ Cannot send cancel: ESP32 not connected")
            return False
    
    def add_web(self, ws):
        self.web_clients.add(ws)
        logger.debug(f"Web client added. Total: {len(self.web_clients)}")
        # Send current ESP32 status immediately
        asyncio.create_task(self.send_esp32_status(ws))
    
    async def send_esp32_status(self, ws):
        """Send current ESP32 connection status to a specific web client"""
        try:
            await ws.send_json({
                'type': 'esp32_status',
                'connected': self.esp32_connected and self.esp32 is not None
            })
        except:
            pass
    
    def remove_web(self, ws):
        self.web_clients.discard(ws)
        logger.debug(f"Web client removed. Total: {len(self.web_clients)}")
    
    def set_esp32(self, ws):
        self.esp32 = ws
        self.esp32_connected = True
        logger.info("✅ ESP32 connection set")
        # Broadcast to all web clients that ESP32 is connected
        asyncio.create_task(self.broadcast({
            'type': 'esp32_status',
            'connected': True
        }))
    
    def clear_esp32(self):
        self.esp32 = None
        self.esp32_connected = False
        logger.info("❌ ESP32 connection cleared")
        # Broadcast to all web clients that ESP32 is disconnected
        asyncio.create_task(self.broadcast({
            'type': 'esp32_status',
            'connected': False
        }))

manager = WSManager()