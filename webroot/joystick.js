/*
 * CamCar virtual joystick -- a DOM-based control based on the JoyStick project
 * (https://github.com/bobboteck/JoyStick) by Roberto D'Amico.
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 Roberto D'Amico (Bobboteck).
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Each instance tracks its own touch by id so two joysticks can be driven
 * simultaneously (multi-touch), and recomputes its geometry on resize/drag so
 * it stays correct across orientation changes.
 */
class Joystick {
    constructor(container, onChange) {
        this.container = container;
        this.thumb = container.querySelector('.joystick-thumb');
        this.base = container.querySelector('.joystick-base');
        this.onChange = onChange;
        this.active = false;
        this.centerX = 0;
        this.centerY = 0;
        this.maxDistance = 0;
        this.currentX = 0;
        this.currentY = 0;
        this.touchId = null;   // identifier of the touch this joystick owns

        this.init();
    }

    updateGeometry() {
        const rect = this.base.getBoundingClientRect();
        this.centerX = rect.width / 2;
        this.centerY = rect.height / 2;
        this.maxDistance = rect.width / 2 - this.thumb.clientWidth / 2;
    }

    init() {
        this.updateGeometry();
        window.addEventListener('resize', () => this.updateGeometry());

        // Touch events. passive:false so preventDefault() can stop the
        // page scrolling/zooming while a joystick is being dragged.
        this.container.addEventListener('touchstart', (e) => this.handleStart(e), { passive: false });
        document.addEventListener('touchmove', (e) => this.handleMove(e), { passive: false });
        document.addEventListener('touchend', (e) => this.handleEnd(e), { passive: false });
        document.addEventListener('touchcancel', (e) => this.handleEnd(e), { passive: false });

        // Mouse events
        this.container.addEventListener('mousedown', (e) => this.handleStart(e));
        document.addEventListener('mousemove', (e) => this.handleMove(e));
        document.addEventListener('mouseup', (e) => this.handleEnd(e));
    }

    handleStart(e) {
        if (this.active) return;   // already own a touch
        this.updateGeometry();     // fresh center/radius (orientation-proof)
        if (e.type === 'mousedown') {
            if (e.target === this.thumb || e.target === this.base) {
                e.preventDefault();
                this.active = true;
                this.touchId = 'mouse';
                this.container.style.opacity = '1';
            }
            return;
        }
        // Touch: claim the new touch that landed on THIS joystick, by id,
        // so the two joysticks can be driven independently (multi-touch).
        for (let i = 0; i < e.changedTouches.length; i++) {
            const t = e.changedTouches[i];
            if (this.container.contains(t.target)) {
                e.preventDefault();
                this.active = true;
                this.touchId = t.identifier;
                this.container.style.opacity = '1';
                break;
            }
        }
    }

    // Return this joystick's tracked pointer from the event, or null.
    getPoint(e) {
        if (this.touchId === 'mouse') return e;
        for (let i = 0; i < e.touches.length; i++) {
            if (e.touches[i].identifier === this.touchId) return e.touches[i];
        }
        return null;
    }

    handleMove(e) {
        if (!this.active) return;
        const touch = this.getPoint(e);
        if (!touch) return;   // a different finger / joystick moved
        e.preventDefault();

        const rect = this.base.getBoundingClientRect();
        let x = touch.clientX - rect.left;
        let y = touch.clientY - rect.top;

        // Calculate distance from center
        const deltaX = x - this.centerX;
        const deltaY = y - this.centerY;
        const distance = Math.sqrt(deltaX * deltaX + deltaY * deltaY);

        // If distance is greater than max, scale it down
        if (distance > this.maxDistance) {
            x = this.centerX + (deltaX / distance) * this.maxDistance;
            y = this.centerY + (deltaY / distance) * this.maxDistance;
        }

        // Update thumb position
        this.thumb.style.left = `${x - this.thumb.clientWidth / 2}px`;
        this.thumb.style.top = `${y - this.thumb.clientHeight / 2}px`;

        // Calculate normalized values (-100 to 100)
        this.currentX = Math.round((x - this.centerX) / this.maxDistance * 100);
        this.currentY = Math.round((y - this.centerY) / this.maxDistance * -100); // Invert Y axis

        if (this.onChange) {
            this.onChange(this.currentX, this.currentY);
        }
    }

    handleEnd(e) {
        if (!this.active) return;
        if (this.touchId === 'mouse') {
            if (e.type !== 'mouseup') return;
        } else {
            let ended = false;
            for (let i = 0; i < e.changedTouches.length; i++) {
                if (e.changedTouches[i].identifier === this.touchId) { ended = true; break; }
            }
            if (!ended) return;   // some other finger lifted
        }
        e.preventDefault();
        this.active = false;
        this.touchId = null;
        this.container.style.opacity = '0.7';

        // Reset thumb position
        this.thumb.style.left = '35%';
        this.thumb.style.top = '35%';
        this.currentX = 0;
        this.currentY = 0;

        if (this.onChange) {
            this.onChange(0, 0);
        }
    }
}
