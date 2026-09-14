document.addEventListener('DOMContentLoaded', () => {
    const diskSelect = document.getElementById('diskSelect');
    const refreshDisksBtn = document.getElementById('refreshDisksBtn');
    const convertBtn = document.getElementById('convertBtn');
    const restoreBtn = document.getElementById('restoreBtn');
    const statusArea = document.getElementById('statusArea');
    const statusText = document.getElementById('statusText');
    const spinner = document.getElementById('spinner');
    const progressBarContainer = document.getElementById('progressBarContainer');
    const progressBar = document.getElementById('progressBar');
    const updateBanner = document.getElementById('updateBanner');
    const updateText = document.getElementById('updateText');
    const updateLink = document.getElementById('updateLink');
    
    const CURRENT_VERSION = 'v1.0.0';
    let currentDisks = [];
    let progressInterval = null;

    function startOperation(initialText) {
        convertBtn.disabled = true;
        restoreBtn.disabled = true;
        refreshDisksBtn.disabled = true;
        diskSelect.disabled = true;

        statusArea.className = 'card status-card';
        statusArea.classList.remove('hidden');
        spinner.style.display = 'block';
        statusText.textContent = initialText;

        progressBarContainer.classList.remove('hidden');
        progressBar.style.width = '0%';

        if (progressInterval) clearInterval(progressInterval);
        progressInterval = setInterval(async () => {
            try {
                const res = await fetch('/api/progress');
                if (res.ok) {
                    const data = await res.json();
                    if (data.text) {
                        statusText.innerText = data.text;
                    }
                    progressBar.style.width = Math.max(5, data.percent) + '%';
                }
            } catch (e) {
                // Ignore fetch errors during polling
            }
        }, 400);
    }

    function endOperation(success, message) {
        if (progressInterval) {
            clearInterval(progressInterval);
            progressInterval = null;
        }

        convertBtn.disabled = false;
        restoreBtn.disabled = false;
        refreshDisksBtn.disabled = false;
        diskSelect.disabled = false;

        spinner.style.display = 'none';
        progressBarContainer.classList.add('hidden');

        if (success) {
            statusArea.className = 'card status-card success';
            statusText.textContent = message;
            setTimeout(() => {
                if (statusArea.classList.contains('success')) {
                    statusArea.classList.add('hidden');
                }
            }, 8000);
        } else {
            statusArea.className = 'card status-card error';
            statusText.textContent = 'Error: ' + message;
        }
    }

    function updateConvertButton() {
        if (!diskSelect.value || currentDisks.length === 0) {
            convertBtn.textContent = 'Convert File System';
            return;
        }
        const disk = currentDisks.find(d => d.letter === diskSelect.value);
        if (disk) {
            if (disk.fs.toUpperCase().includes('EXT')) {
                convertBtn.textContent = 'Convert to FAT32 / exFAT';
            } else {
                convertBtn.textContent = 'Convert to EXT4';
            }
        }
    }

    async function loadDisks() {
        refreshDisksBtn.disabled = true;
        diskSelect.disabled = true;
        diskSelect.innerHTML = '<option value="">Loading drives...</option>';
        try {
            const response = await fetch('/api/disks');
            if (!response.ok) throw new Error('Failed to fetch disks');
            currentDisks = await response.json();
            
            diskSelect.innerHTML = '';
            if (currentDisks.length === 0) {
                diskSelect.innerHTML = '<option value="">No USB drives found</option>';
            } else {
                currentDisks.forEach(disk => {
                    const option = document.createElement('option');
                    option.value = disk.letter;
                    option.textContent = `${disk.letter}: - ${disk.name} (${disk.fs})`;
                    diskSelect.appendChild(option);
                });
            }
            updateConvertButton();
        } catch (error) {
            console.error(error);
            diskSelect.innerHTML = '<option value="">Error loading drives</option>';
        } finally {
            refreshDisksBtn.disabled = false;
            diskSelect.disabled = false;
        }
    }

    async function performAction(endpoint, actionName, confirmMsg) {
        const disk = diskSelect.value;
        if (!disk) {
            alert('Please select a USB drive first.');
            return false;
        }

        if (confirmMsg && !confirm(confirmMsg.replace('{disk}', disk))) {
            return false;
        }

        startOperation(`${actionName} in progress. Please wait...`);

        try {
            const response = await fetch(endpoint, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ disk: disk })
            });
            
            const result = await response.json();
            if (response.ok && result.success) {
                endOperation(true, `${actionName} completed successfully!`);
                loadDisks();
                return true;
            } else {
                throw new Error(result.error || `Unknown error during ${actionName}`);
            }
        } catch (error) {
            console.error(error);
            endOperation(false, error.message);
            return false;
        }
    }

    refreshDisksBtn.addEventListener('click', loadDisks);
    diskSelect.addEventListener('change', updateConvertButton);
    
    convertBtn.addEventListener('click', async () => {
        const diskLetter = diskSelect.value;
        if (!diskLetter) {
            alert('Please select a USB drive first.');
            return;
        }
        const disk = currentDisks.find(d => d.letter === diskLetter);
        if (!disk) return;
        
        const isExt = disk.fs.toUpperCase().includes('EXT');
        const targetFsName = isExt ? 'FAT32 / exFAT' : 'EXT4';
        const endpoint = isExt ? '/api/format/exfat' : '/api/format/ext4';
        
        if (!confirm(`This will BACKUP all files on drive ${diskLetter}: (if readable), then FORMAT it to ${targetFsName}.\n\nDo you want to proceed?`)) {
            return;
        }
        
        startOperation(`Step 1/2: Backing up data on drive ${diskLetter}:...`);
        
        try {
            const backupRes = await fetch('/api/backup', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ disk: diskLetter })
            });
            const backupResult = await backupRes.json();
            
            if (!backupRes.ok || !backupResult.success) {
                throw new Error(backupResult.error || "Backup failed.");
            }
            
            statusText.innerText = `Step 2/2: Formatting ${diskLetter}: to ${targetFsName}...`;
            const formatRes = await fetch(endpoint, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ disk: diskLetter })
            });
            const formatResult = await formatRes.json();
            
            if (!formatRes.ok || !formatResult.success) {
                throw new Error(formatResult.error || "Formatting failed.");
            }
            
            let successMsg = `Drive formatted to ${targetFsName} successfully!`;
            if (!isExt) {
                successMsg = `Drive successfully formatted to EXT4! (Windows Explorer shows it as unformatted/RAW because Windows has no EXT4 driver, but it works in Linux/Android/Steam Deck).`;
            }
            endOperation(true, successMsg);
            loadDisks();
        } catch (error) {
            endOperation(false, error.message);
        }
    });

    restoreBtn.addEventListener('click', () => {
        performAction('/api/restore', 'Restore', 'Please select a backup archive to restore to drive {disk}:.\nNote: This will overwrite existing files with the same name.');
    });

    async function checkForUpdates() {
        try {
            const res = await fetch('https://api.github.com/repos/Lorien-Git/WebConventer-USB/releases/latest', {
                headers: { 'Accept': 'application/vnd.github.v3+json' }
            });
            if (res.ok) {
                const data = await res.json();
                if (data.tag_name && data.tag_name !== CURRENT_VERSION) {
                    updateText.textContent = `New release available: ${data.tag_name}!`;
                    if (data.html_url) updateLink.href = data.html_url;
                    updateBanner.classList.remove('hidden');
                }
            }
        } catch (e) {
            // Silently ignore if offline
        }
    }

    // Initial load
    loadDisks();
    checkForUpdates();
});
