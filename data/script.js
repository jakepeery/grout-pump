var gateway = `ws://${window.location.hostname}/ws`;
var websocket;

window.addEventListener('load', onLoad);

function onLoad(event) {
    initWebSocket();
    setupFormValidation();
}

function initWebSocket() {
    console.log('Trying to open a WebSocket connection...');
    websocket = new WebSocket(gateway);
    websocket.onopen = onOpen;
    websocket.onclose = onClose;
    websocket.onmessage = onMessage;
}

function onOpen(event) {
    console.log('Connection opened');
    const header = document.querySelector('h1');
    if(header) {
        if(!header.dataset.originalText) header.dataset.originalText = header.textContent;
        header.textContent = header.dataset.originalText + ' (Connected 🟢)';
    }
}

function onClose(event) {
    console.log('Connection closed');
    const header = document.querySelector('h1');
    if(header && header.dataset.originalText) {
        header.textContent = header.dataset.originalText + ' (Disconnected 🔴)';
    }
    setTimeout(initWebSocket, 2000);
}

function onMessage(event) {
    var data = JSON.parse(event.data);
    updateUI(data);
}

function updateUI(data) {
    // Handle E-Stop State
    const estopAlert = document.getElementById('estop-alert');
    const estopStatus = document.getElementById('estop-status');
    const pumpContainer = document.querySelector('.pump-container');
    const animStatus = document.getElementById('anim-status-text');
    
    // Update Stats
    const lastCycleEl = document.getElementById('last-cycle');
    const avgCycleEl = document.getElementById('avg-cycle');
    if (lastCycleEl) lastCycleEl.textContent = data.lastDuration > 0 ? data.lastDuration : '--';
    if (avgCycleEl) avgCycleEl.textContent = data.avgDuration > 0 ? data.avgDuration : '--';

    // Draw Chart if history exists
    if (data.history && Array.isArray(data.history)) {
        drawChart(data.history);
    }

    // Pump Animation Logic
    if (pumpContainer && animStatus) {
        const pistonHead = document.querySelector('.piston-head');
        let direction = "";
        
        // Constants for animation
        const MIN_POS = 10;
        const MAX_POS = 280;
        const FULL_TRAVEL = MAX_POS - MIN_POS;
        
        // Determine Animation Duration (Speed)
        // Use average duration to set REALISTIC speed if available logic
        // If data.avgDuration is valid (>500ms), use it. Subtract delay? 
        // Logic: avgDuration is "Stroke Time". 
        // If avgDuration is 5000ms, then FULL_TIME should be 5.0.
        let FULL_TIME = 3.0; // Default fallback
        if (data.avgDuration && data.avgDuration > 500) {
            FULL_TIME = data.avgDuration / 1000.0;
        }

        // Determine Direction based on GPO states
        // GPO1 = IN (Retract to Left) based on user preference
        if (data.gpo1 === 1) { 
            direction = "RETRACTING (IN)";
            animStatus.style.color = '#2196f3'; // Blue
            
            // Only trigger if not already retrecting
            if (!pumpContainer.classList.contains('anim-retract')) {
                 // Calculate duration based on remaining distance to MIN_POS (Left)
                const currentLeft = parseFloat(getComputedStyle(pistonHead).left) || MIN_POS;
                const distance = Math.abs(currentLeft - MIN_POS);
                const duration = (distance / FULL_TRAVEL) * FULL_TIME;

                pistonHead.style.transition = `left ${duration}s linear`;
                pistonHead.style.left = ''; // Clear inline freeze
                
                pumpContainer.classList.remove('anim-extend');
                pumpContainer.classList.add('anim-retract');
                console.log("Pump State: RETRACTING (IN)");
            }
        } else if (data.gpo2 === 1) { // GPO2 = OUT (Extend to Right)
            direction = "EXTENDING (OUT)";
            animStatus.style.color = '#e91e63'; // Red/Pink
            
            if (!pumpContainer.classList.contains('anim-extend')) {
                // Calculate duration based on remaining distance to MAX_POS (Right)
                const currentLeft = parseFloat(getComputedStyle(pistonHead).left) || MIN_POS;
                const distance = Math.abs(MAX_POS - currentLeft);
                const duration = (distance / FULL_TRAVEL) * FULL_TIME;
                
                // Set dynamic transition and target
                pistonHead.style.transition = `left ${duration}s linear`;
                pistonHead.style.left = ''; // Clear inline left
                
                pumpContainer.classList.remove('anim-retract');
                pumpContainer.classList.add('anim-extend');
                console.log("Pump State: EXTENDING (OUT)");
            }
        } else {
            // STOPPED
            direction = "STOPPED";
            animStatus.style.color = '#666';
            
            // If currently animating, freeze it
            if (pumpContainer.classList.contains('anim-extend') || pumpContainer.classList.contains('anim-retract')) {
                const computedStyle = window.getComputedStyle(pistonHead);
                const currentLeft = computedStyle.getPropertyValue('left');
                
                // Freeze
                pistonHead.style.transition = 'none';
                pistonHead.style.left = currentLeft;
                
                pumpContainer.classList.remove('anim-extend', 'anim-retract');
                console.log("Pump State: STOPPED at " + currentLeft);
            }
        }
        
        animStatus.textContent = direction;
    }

    if (data.estopActive) {
        if (estopAlert) estopAlert.style.display = 'block';
        if (estopStatus) {
            estopStatus.textContent = 'ACTIVATED';
            estopStatus.style.color = 'red';
            estopStatus.style.fontWeight = 'bold';
        }
    } else {
            if (estopAlert) estopAlert.style.display = 'none';
            if (estopStatus) {
                estopStatus.textContent = 'OK';
                estopStatus.style.color = '#4caf50';
                estopStatus.style.fontWeight = 'normal';
            }
        }

        // Update mode
        const modeElement = document.getElementById('mode');
        if (modeElement) {
            modeElement.textContent = data.mode;
            
            // Update status box styling
            const statusBox = document.querySelector('.status');
            if (statusBox) {
                statusBox.className = 'status ' + (data.mode === 'MANUAL' ? 'manual' : 'auto');
            }
        }
        
        // Update cycle direction
        const dirElement = document.getElementById('cycle-direction');
        const dirContainer = document.getElementById('cycle-direction-container');
        if (dirElement && dirContainer) {
            if (data.mode === 'AUTO') {
                dirContainer.style.display = 'block';
                dirElement.textContent = data.cycleDirection;
            } else {
                dirContainer.style.display = 'none';
            }
        }
        
        // Update GPO states with indicators
        updateGPOStatus('gpo1', data.gpo1);
        updateGPOStatus('gpo2', data.gpo2);
        
        // Update end-stop states
        updateEndStopStatus('endstop-in', data.endStopIn);
        updateEndStopStatus('endstop-out', data.endStopOut);
        
        // Update Wireless Input states
        updateGPOStatus('input-a', data.inputA);
        updateGPOStatus('input-b', data.inputB);
        updateGPOStatus('input-c', data.inputC);
        updateGPOStatus('input-d', data.inputD);
        
        // Update network information
        const wifiStatus = document.getElementById('wifi-status');
        if (wifiStatus) {
            wifiStatus.innerHTML = '<strong>WiFi:</strong> ' + 
                (data.wifiConnected ? 'Connected to ' + data.wifiSSID : 'AP Mode (Setup)');
        }
        
        const ipAddress = document.getElementById('ip-address');
        if (ipAddress) {
            ipAddress.innerHTML = '<strong>IP Address:</strong> ' + data.ipAddress;
        }
}

function setupFormValidation() {
    const timeoutInput = document.querySelector('input[name="timeout"]');
    if (timeoutInput) {
        timeoutInput.addEventListener('input', function() {
            validateTimeout(this);
        });
        
        const form = timeoutInput.closest('form');
        if (form) {
            form.addEventListener('submit', function(e) {
                if (!validateTimeout(timeoutInput)) {
                    e.preventDefault();
                    alert('Please enter a valid timeout value between 1000 and 300000 ms');
                }
            });
        }
    }
}

function updateGPOStatus(elementId, state) {
    const element = document.getElementById(elementId);
    if (element) {
        const indicator = element.querySelector('.indicator');
        if (indicator) {
            indicator.className = state ? 'indicator on' : 'indicator off';
        }
        const text = element.querySelector('.status-text');
        if (text) {
            text.textContent = state ? 'ON' : 'OFF';
        }
    }
}

function updateEndStopStatus(elementId, state) {
    const element = document.getElementById(elementId);
    if (element) {
        element.textContent = state ? 'TRIGGERED' : 'Open';
        element.style.color = state ? '#f44336' : '#4caf50';
        element.style.fontWeight = state ? 'bold' : 'normal';
    }
}

// Form validation
function validateTimeout(input) {
    const value = parseInt(input.value);
    const min = 1000;
    const max = 300000;
    
    if (isNaN(value) || value < min || value > max) {
        input.style.borderColor = '#f44336';
        showValidationMessage(input, 'Value must be between 1,000 and 300,000 ms');
        return false;
    } else {
        input.style.borderColor = '#4caf50';
        clearValidationMessage(input);
        return true;
    }
}

function showValidationMessage(input, message) {
    let msgElement = input.nextElementSibling;
    if (!msgElement || !msgElement.classList.contains('validation-message')) {
        msgElement = document.createElement('div');
        msgElement.className = 'validation-message';
        msgElement.style.color = '#f44336';
        msgElement.style.fontSize = '0.9em';
        msgElement.style.marginTop = '5px';
        input.parentNode.insertBefore(msgElement, input.nextSibling);
    }
    msgElement.textContent = message;
}

function clearValidationMessage(input) {
    const msgElement = input.nextElementSibling;
    if (msgElement && msgElement.classList.contains('validation-message')) {
        msgElement.remove();
    }
}

// Simple Chart Drawing Function (No external libraries)
function drawChart(history) {
    const canvas = document.getElementById('cycleChart');
    if (!canvas) return;
    
    const ctx = canvas.getContext('2d');
    const width = canvas.width = canvas.parentElement.clientWidth;
    const height = canvas.height = canvas.parentElement.clientHeight;
    
    // Clear
    ctx.clearRect(0, 0, width, height);
    
    if (history.length < 2) {
        ctx.fillStyle = "#999";
        ctx.textAlign = "center";
        ctx.fillText("Need at least 2 cycles for graph...", width/2, height/2);
        return;
    }

    // Determine scale
    let minVal = Math.min(...history);
    let maxVal = Math.max(...history);
    
    // Add padding to scale (10%)
    const range = maxVal - minVal;
    if (range === 0) {
        minVal -= 100;
        maxVal += 100;
    } else {
        minVal -= range * 0.1;
        maxVal += range * 0.1;
    }
    if (minVal < 0) minVal = 0;

    const plotRange = maxVal - minVal;
    
    // Draw Background Grid
    ctx.strokeStyle = "#eee";
    ctx.beginPath();
    ctx.moveTo(0, height/2);
    ctx.lineTo(width, height/2);
    ctx.stroke();

    // Draw Line
    ctx.strokeStyle = "#00bcd4";
    ctx.lineWidth = 3;
    ctx.lineJoin = "round";
    ctx.beginPath();
    
    const xStep = width / (history.length - 1);
    
    history.forEach((val, index) => {
        const x = index * xStep;
        // Invert Y (Canvas 0 is top)
        const y = height - ((val - minVal) / plotRange) * height;
        
        if (index === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
    });
    ctx.stroke();
    
    // Draw Points
    ctx.fillStyle = "#00838f";
    history.forEach((val, index) => {
        const x = index * xStep;
        const y = height - ((val - minVal) / plotRange) * height;
        
        ctx.beginPath();
        ctx.arc(x, y, 4, 0, Math.PI * 2);
        ctx.fill();
    });
}

// Initialization handled by window.load and setupFormValidation in main script
