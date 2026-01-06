// Auto-refresh functionality for the home page
let autoRefreshEnabled = false;
let refreshInterval = null;

function toggleAutoRefresh() {
    autoRefreshEnabled = !autoRefreshEnabled;
    const btn = document.getElementById('auto-refresh-btn');
    
    if (autoRefreshEnabled) {
        btn.textContent = '🔄 Auto-Refresh: ON';
        btn.style.background = 'linear-gradient(135deg, #4CAF50 0%, #45a049 100%)';
        startAutoRefresh();
    } else {
        btn.textContent = '🔄 Auto-Refresh: OFF';
        btn.style.background = 'linear-gradient(135deg, #667eea 0%, #764ba2 100%)';
        stopAutoRefresh();
    }
}

function startAutoRefresh() {
    if (refreshInterval) {
        clearInterval(refreshInterval);
    }
    refreshInterval = setInterval(updateStatus, 2000); // Update every 2 seconds
}

function stopAutoRefresh() {
    if (refreshInterval) {
        clearInterval(refreshInterval);
        refreshInterval = null;
    }
}

async function updateStatus() {
    try {
        const response = await fetch('/status');
        const data = await response.json();
        
        // Handle E-Stop State
        const estopAlert = document.getElementById('estop-alert');
        const estopStatus = document.getElementById('estop-status');
        
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
        
    } catch (error) {
        console.error('Failed to update status:', error);
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

// Initialize on page load
document.addEventListener('DOMContentLoaded', function() {
    // Add event listener for timeout validation
    const timeoutInput = document.querySelector('input[name="timeout"]');
    if (timeoutInput) {
        timeoutInput.addEventListener('input', function() {
            validateTimeout(this);
        });
        
        // Validate on form submit
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
    
    // Add auto-refresh button if on home page
    if (document.getElementById('mode')) {
        const navButtons = document.querySelector('.nav-buttons');
        if (navButtons) {
            const refreshBtn = document.createElement('button');
            refreshBtn.id = 'auto-refresh-btn';
            refreshBtn.className = 'btn';
            refreshBtn.textContent = '🔄 Auto-Refresh: OFF';
            refreshBtn.onclick = toggleAutoRefresh;
            navButtons.insertBefore(refreshBtn, navButtons.firstChild);
        }
    }
});

// Clean up on page unload
window.addEventListener('beforeunload', function() {
    stopAutoRefresh();
});
