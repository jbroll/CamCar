#!/usr/bin/env python3
#
from fastapi import FastAPI, WebSocket, Request
from fastapi.responses import HTMLResponse
from fastapi.staticfiles import StaticFiles
import uvicorn
import asyncio
import logging

# Set up logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

app = FastAPI()

# Create an HTML response with the content of your index.html
@app.get("/", response_class=HTMLResponse)
async def get_html():
    with open("index.html", "r") as f:
        return f.read()

# WebSocket endpoint for car control
@app.websocket("/CarInput")
async def websocket_car_input(websocket: WebSocket):
    await websocket.accept()
    logger.info("Car control WebSocket connected")
    
    try:
        while True:
            data = await websocket.receive_text()
            key, value = data.split(",")
            logger.info(f"Received command: {key} with value: {value}")
            
            # Simulate car responses
            if key == "MoveCar":
                direction_map = {
                    "0": "STOP",
                    "1": "FORWARD",
                    "2": "BACKWARD",
                    "3": "LEFT",
                    "4": "RIGHT"
                }
                logger.info(f"Moving car: {direction_map.get(value, 'UNKNOWN')}")
            elif key == "Speed":
                logger.info(f"Setting speed to: {value}")
            elif key == "Light":
                logger.info(f"Setting light to: {value}")
            elif key == "Pan":
                logger.info(f"Panning camera to: {value}")
            elif key == "Tilt":
                logger.info(f"Tilting camera to: {value}")
                
    except Exception as e:
        logger.error(f"WebSocket error: {e}")
    finally:
        logger.info("Car control WebSocket disconnected")

# WebSocket endpoint for camera feed
@app.websocket("/Camera")
async def websocket_camera(websocket: WebSocket):
    await websocket.accept()
    logger.info("Camera WebSocket connected")
    
    try:
        # Just keep the connection alive
        while True:
            await asyncio.sleep(1)
    except Exception as e:
        logger.error(f"Camera WebSocket error: {e}")
    finally:
        logger.info("Camera WebSocket disconnected")

if __name__ == "__main__":
    logger.info("Starting test server for CamCar interface")
    uvicorn.run(app, host="0.0.0.0", port=8000)
