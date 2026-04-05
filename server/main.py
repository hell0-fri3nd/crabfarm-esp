from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.staticfiles import StaticFiles
from fastapi.responses import HTMLResponse
import json
import asyncio
from datetime import datetime
from contextlib import asynccontextmanager
import os
import logging

from config import WS_PORT, WEB_WS_PORT, HTTP_PORT
from database import init_db, save_test_record, get_last_5_records
from websocket_manager import manager

# Setup logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

# Global sensor data storage
current_sensors = {
    'do': -1, 'temperature': -1, 'compensated_do': -1, 'ph': -1,
    'tds': -1, 'turbidity': -1, 'ammonium': -1, 'weight': -1,
    'water_level': -1, 'water_percent': 0, 'water_detected': False,
    'test_state': 0
}

@asynccontextmanager
async def lifespan(app: FastAPI):
    init_db()
    logger.info(f"FastAPI server started on port {HTTP_PORT}")
    logger.info(f"ESP32 WebSocket port: {WS_PORT}")
    logger.info(f"Web client WebSocket port: {WEB_WS_PORT}")
    logger.info("=========================================")
    yield

app = FastAPI(lifespan=lifespan)

# Mount static files
app.mount("/static", StaticFiles(directory="static"), name="static")

# ========== ESP32 WebSocket Handler ==========
@app.websocket("/esp32")
async def esp32_websocket(websocket: WebSocket):
    await websocket.accept()
    manager.set_esp32(websocket)
    logger.info("ESP32 connected to WebSocket")
    
    try:
        while True:
            data = await websocket.receive_text()
            logger.debug(f"Received from ESP32: {data[:100]}...")  # Log first 100 chars
            
            try:
                sensor_data = json.loads(data)
                
                # Update current sensor values
                for key in current_sensors:
                    if key in sensor_data:
                        current_sensors[key] = sensor_data[key]
                
                # Check if this is a record request (test_state == 4)
                if sensor_data.get('test_state') == 4:
                    logger.info("Recording test data from ESP32")
                    save_test_record(sensor_data)
                    await manager.broadcast({
                        'type': 'new_record',
                        'records': get_last_5_records()
                    })
                
                # Broadcast to web clients
                await manager.broadcast({
                    'type': 'sensor_update',
                    'data': current_sensors,
                    'timestamp': datetime.now().strftime('%H:%M:%S')
                })
                
            except json.JSONDecodeError:
                # Handle plain text commands
                if data == "RECORD_CONFIRM":
                    logger.info("Record confirmation received from ESP32")
                elif data == "TEST_COMPLETE":
                    logger.info("Test complete from ESP32")
                else:
                    logger.info(f"Plain text from ESP32: {data}")
                    
    except WebSocketDisconnect:
        logger.warning("ESP32 disconnected")
        manager.clear_esp32()
    except Exception as e:
        logger.error(f"Error in ESP32 WebSocket: {e}")
        manager.clear_esp32()

# ========== Web Client WebSocket Handler ==========
@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    manager.add_web(websocket)
    logger.info(f"Web client connected. Total clients: {len(manager.web_clients)}")
    
    # Send initial data
    await websocket.send_json({
        'type': 'init',
        'data': current_sensors,
        'records': get_last_5_records()
    })
    logger.info("Initial data sent to web client")
    
    try:
        while True:
            data = await websocket.receive_text()
            msg = json.loads(data)
            logger.info(f"Received from web client: {msg.get('type')}")
            
            if msg.get('type') == 'start_test':
                logger.info("Start test command received from web client, forwarding to ESP32")
                await manager.send_to_esp32("START_TEST")
                await websocket.send_json({'type': 'test_started'})
                
    except WebSocketDisconnect:
        manager.remove_web(websocket)
        logger.info(f"Web client disconnected. Total clients: {len(manager.web_clients)}")
    except Exception as e:
        logger.error(f"Error in web WebSocket: {e}")
        manager.remove_web(websocket)

# ========== HTTP Endpoints ==========
@app.get("/")
async def get_index():
    html_path = os.path.join("static", "index.html")
    if os.path.exists(html_path):
        with open(html_path, "r", encoding="utf-8") as f:
            return HTMLResponse(content=f.read())
    else:
        logger.error(f"index.html not found at {html_path}")
        return HTMLResponse(content="<h1>index.html not found</h1>", status_code=404)

@app.get("/api/records")
async def get_records():
    records = get_last_5_records()
    return {"records": records}

@app.get("/api/status")
async def get_status():
    return {
        "esp32_connected": manager.esp32 is not None,
        "web_clients": len(manager.web_clients),
        "last_sensor_update": current_sensors
    }

# ========== Run Server ==========
if __name__ == "__main__":
    import uvicorn
    logger.info(f"Starting server on 0.0.0.0:{HTTP_PORT}")
    uvicorn.run(app, host="0.0.0.0", port=HTTP_PORT, log_level="info")