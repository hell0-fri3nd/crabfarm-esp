import json
from typing import Set, Optional
import logging

logger = logging.getLogger(__name__)

class WSManager:
    def __init__(self):
        self.esp32: Optional = None
        self.web_clients: Set = set()
    
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
    
    async def send_to_esp32(self, message: str):
        if self.esp32:
            try:
                await self.esp32.send(message)
                logger.info(f"Sent to ESP32: {message}")
            except Exception as e:
                logger.error(f"Failed to send to ESP32: {e}")
        else:
            logger.warning("Cannot send to ESP32: Not connected")
    
    def add_web(self, ws):
        self.web_clients.add(ws)
        logger.debug(f"Web client added. Total: {len(self.web_clients)}")
    
    def remove_web(self, ws):
        self.web_clients.discard(ws)
        logger.debug(f"Web client removed. Total: {len(self.web_clients)}")
    
    def set_esp32(self, ws):
        self.esp32 = ws
        logger.info("ESP32 connection set")
    
    def clear_esp32(self):
        self.esp32 = None
        logger.info("ESP32 connection cleared")

manager = WSManager()