 import { initializeApp } from "https://www.gstatic.com/firebasejs/10.7.1/firebase-app.js";
        import { getDatabase, ref, onValue, set, get, update } from "https://www.gstatic.com/firebasejs/10.7.1/firebase-database.js";
        import { getAuth, signInWithPopup, GoogleAuthProvider, onAuthStateChanged, signOut, signInWithEmailAndPassword, createUserWithEmailAndPassword } from "https://www.gstatic.com/firebasejs/10.7.1/firebase-auth.js";

        const firebaseConfig = {
          apiKey: "AIzaSyB8XmbJmsIMttA-FYlvP9ygRlW59WBUo50",
          authDomain: "smart-fish-breeder.firebaseapp.com",
          databaseURL: "https://smart-fish-breeder-default-rtdb.firebaseio.com",
          projectId: "smart-fish-breeder",
          storageBucket: "smart-fish-breeder.firebasestorage.app",
          messagingSenderId: "772950657962",
          appId: "1:772950657962:web:02fde1c3010e7c467979d3"
        };

        const app = initializeApp(firebaseConfig);
        const db = getDatabase(app);
        const auth = getAuth(app);
        const provider = new GoogleAuthProvider();

        let currentUser = null;
        let isLoginMode = true;
        let offlineTimer;
        let lastPing = null;

        onAuthStateChanged(auth, (user) => {
            currentUser = user;
            if (user) {
                closeAuthModal();
                const name = user.displayName || user.email.split('@')[0];
                const photo = user.photoURL || `https://ui-avatars.com/api/?name=${name}&background=eff6ff&color=3b82f6`;
                document.getElementById('header-profile-img').src = photo;
                document.getElementById('modal-profile-img').src = photo;
                document.getElementById('modal-profile-name').innerText = name;
                document.getElementById('modal-profile-email').innerText = user.email;
            } else {
                document.getElementById('header-profile-img').src = "https://ui-avatars.com/api/?name=Guest&background=e2e8f0&color=64748b";
            }
        });

        window.requireAuth = function(actionCallback) {
            if (currentUser) {
                actionCallback();
            } else {
                openAuthModal();
            }
        };

        window.openAuthModal = function() {
            document.getElementById('auth-modal').classList.remove('hidden');
            document.getElementById('auth-modal').classList.add('flex');
        };

        window.closeAuthModal = function() {
            document.getElementById('auth-modal').classList.add('hidden');
            document.getElementById('auth-modal').classList.remove('flex');
        };

        window.toggleAuthMode = function() {
            isLoginMode = !isLoginMode;
            if(isLoginMode) {
                document.getElementById('auth-title').innerText = "Welcome Back";
                document.getElementById('auth-subtitle').innerText = "Please login to control devices";
                document.getElementById('auth-action-btn').innerText = "Sign In";
                document.getElementById('auth-toggle-text').innerText = "Don't have an account?";
            } else {
                document.getElementById('auth-title').innerText = "Create Account";
                document.getElementById('auth-subtitle').innerText = "Sign up to join the system";
                document.getElementById('auth-action-btn').innerText = "Sign Up";
                document.getElementById('auth-toggle-text').innerText = "Already have an account?";
            }
        };

        window.handleGoogleAuth = function() {
            signInWithPopup(auth, provider).catch(error => alert(error.message));
        };

        window.handleEmailAuth = function() {
            const email = document.getElementById('auth-email').value;
            const pass = document.getElementById('auth-password').value;
            if(!email || !pass) return alert("Please fill all fields");
            if(isLoginMode) {
                signInWithEmailAndPassword(auth, email, pass).catch(error => alert(error.message));
            } else {
                createUserWithEmailAndPassword(auth, email, pass).catch(error => alert(error.message));
            }
        };

        window.checkProfile = function() {
            if(currentUser) {
                document.getElementById('profile-modal').classList.remove('hidden');
                document.getElementById('profile-modal').classList.add('flex');
            } else {
                openAuthModal();
            }
        };

        window.closeProfileModal = function() {
            document.getElementById('profile-modal').classList.add('hidden');
            document.getElementById('profile-modal').classList.remove('flex');
        };

        window.logoutUser = function() {
            signOut(auth).then(() => {
                closeProfileModal();
            });
        };

        let localState = {
            temp: 0,
            ph: 0,
            fishMode: 0
        };

        let relayStates = {
            REL_ACID_PUMP: false, REL_ALKALI_PUMP: false, REL_COOLER_FAN: false, REL_WATER_HEATER: false,
            REL_AIR_PUMP: false, REL_WATER_FLOW: false, REL_RAIN_PUMP: false, REL_LIGHT_CTRL: false
        };

        const relayKeys = [
            "REL_ACID_PUMP", "REL_ALKALI_PUMP", "REL_COOLER_FAN", "REL_WATER_HEATER",
            "REL_AIR_PUMP", "REL_WATER_FLOW", "REL_RAIN_PUMP", "REL_LIGHT_CTRL"
        ];

        let dbFishes = [];
        let showAllFishes = false;
        let isCamVisible = false;
        let camPan = 90;
        let camTilt = 90;

        const fishColors = ['text-red-500', 'text-orange-500', 'text-amber-500', 'text-emerald-500', 'text-teal-500', 'text-cyan-500', 'text-blue-500', 'text-indigo-500', 'text-violet-500', 'text-purple-500', 'text-fuchsia-500', 'text-rose-500'];

        const defaultFishes = [
            { id: 0, name: 'Manual', icon: 'fa-hand', color: 'text-slate-400' },
            { id: 1, name: 'Gold Fish', icon: 'fa-fish', color: 'text-orange-400', phMin: 6.5, phMax: 7.5, tempMin: 22, tempMax: 26, flow: true, rain: false },
            { id: 2, name: 'Comet', icon: 'fa-fish', color: 'text-red-400', phMin: 6.0, phMax: 7.5, tempMin: 20, tempMax: 25, flow: true, rain: false },
            { id: 3, name: 'Rohu', icon: 'fa-fish', color: 'text-indigo-400', phMin: 6.5, phMax: 8.0, tempMin: 25, tempMax: 30, flow: false, rain: true }
        ];

        const relaysMeta = [
            { id: "REL_ACID_PUMP", name: "Acid Pump", icon: "fa-flask", color: "from-rose-400 to-red-500", activeBg: "bg-rose-50 border-rose-200" },
            { id: "REL_ALKALI_PUMP", name: "Alkali Pump", icon: "fa-flask", color: "from-blue-400 to-indigo-500", activeBg: "bg-blue-50 border-blue-200" },
            { id: "REL_COOLER_FAN", name: "Cooler Fan", icon: "fa-fan", color: "from-cyan-400 to-blue-500", activeBg: "bg-cyan-50 border-cyan-200" },
            { id: "REL_WATER_HEATER", name: "Water Heater", icon: "fa-fire", color: "from-orange-400 to-red-500", activeBg: "bg-orange-50 border-orange-200" },
            { id: "REL_AIR_PUMP", name: "Air Pump", icon: "fa-wind", color: "from-emerald-400 to-teal-500", activeBg: "bg-emerald-50 border-emerald-200" },
            { id: "REL_WATER_FLOW", name: "Water Flow", icon: "fa-water", color: "from-blue-500 to-blue-700", activeBg: "bg-blue-50 border-blue-200" },
            { id: "REL_RAIN_PUMP", name: "Rain Pump", icon: "fa-cloud-rain", color: "from-indigo-400 to-purple-500", activeBg: "bg-indigo-50 border-indigo-200" },
            { id: "REL_LIGHT_CTRL", name: "UV Light", icon: "fa-lightbulb", color: "from-amber-400 to-orange-500", activeBg: "bg-amber-50 border-amber-200" }
        ];

        function setOfflineState() {
            document.getElementById('system-status-dot').classList.remove('bg-green-500', 'animate-pulse');
            document.getElementById('system-status-dot').classList.add('bg-red-500');
            document.getElementById('system-status-text').innerText = "Offline";
            document.getElementById('system-status-text').classList.remove('text-slate-500');
            document.getElementById('system-status-text').classList.add('text-red-500');
        }

        function setOnlineState() {
            document.getElementById('system-status-dot').classList.remove('bg-red-500');
            document.getElementById('system-status-dot').classList.add('bg-green-500', 'animate-pulse');
            document.getElementById('system-status-text').innerText = "Online";
            document.getElementById('system-status-text').classList.remove('text-red-500');
            document.getElementById('system-status-text').classList.add('text-slate-500');
        }

        onValue(ref(db, 'ping'), (snapshot) => {
            const currentPing = snapshot.val();
            if(currentPing !== lastPing) {
                lastPing = currentPing;
                setOnlineState();
                clearTimeout(offlineTimer);
                offlineTimer = setTimeout(() => {
                    setOfflineState();
                }, 5000);
            }
        });

                window.saveWifiConfig = function() {
    const ssid = document.getElementById('wifi-ssid').value;
    const pass = document.getElementById('wifi-pass').value;
    
    if (!ssid) {
        showAlert("Invalid Input", "Please enter a valid WiFi Name.", "error");
        return;
    }
    
    showLoading();
    set(ref(db, 'newWifi'), { ssid: ssid, pass: pass }).then(() => {
        hideLoading();
        showAlert("Successful", "WiFi credentials sent! Device is rebooting.", "success");
        document.getElementById('wifi-ssid').value = '';
        document.getElementById('wifi-pass').value = '';
    }).catch(() => {
        hideLoading();
        showAlert("Error", "Failed to send WiFi details.", "error");
    });
}
        relayKeys.forEach(key => {
            onValue(ref(db, key), (snapshot) => {
                relayStates[key] = snapshot.val() || false;
                updateRelayVisuals();
            });
        });

        onValue(ref(db, 'temp'), (snapshot) => {
            localState.temp = snapshot.val() || 0;
            updateSensorUI();
        });

        onValue(ref(db, 'ph'), (snapshot) => {
            localState.ph = snapshot.val() || 0;
            updateSensorUI();
        });

        onValue(ref(db, 'fishMode'), (snapshot) => {
            localState.fishMode = snapshot.val() || 0;
            updateCurrentModeUI();
            renderHomeFishes();
        });

        onValue(ref(db, 'fishes'), (snapshot) => {
            const data = snapshot.val();
            if (data) {
                dbFishes = data;
            } else {
                dbFishes = defaultFishes;
                set(ref(db, 'fishes'), dbFishes);
            }
            renderHomeFishes();
            updateCurrentModeUI();
            renderDbFishes();
        });

        onValue(ref(db, 'camera'), (snapshot) => {
            const data = snapshot.val();
            if(data) {
                if(data.pan !== undefined) camPan = data.pan;
                if(data.tilt !== undefined) camTilt = data.tilt;
                if(data.streamUrl) {
                    document.getElementById('cam-stream').src = data.streamUrl;
                }
            }
        });
        
        onValue(ref(db, 'savedWifiConfig'), (snapshot) => {
    const data = snapshot.val();
    if(data) {
        document.getElementById('wifi-ssid').value = data.ssid || '';
        document.getElementById('wifi-pass').value = data.pass || '';
        checkPassLength();
    }
});

        function updateSensorUI() {
            document.getElementById('display-temp').innerText = localState.temp.toFixed(1);
            const tempStatus = document.getElementById('display-temp-status');
            if(localState.temp > 33) { tempStatus.innerText = "Hot"; }
            else if (localState.temp < 24) { tempStatus.innerText = "Cold"; }
            else { tempStatus.innerText = "Optimal"; }

            document.getElementById('display-ph').innerText = localState.ph.toFixed(2);
            const phStatus = document.getElementById('display-ph-status');
            if(localState.ph < 6) { phStatus.innerText = "Acidic"; }
            else if (localState.ph > 8.5) { phStatus.innerText = "Alkaline"; }
            else { phStatus.innerText = "Optimal"; }
        }

        window.renderHomeFishes = function() {
            const grid = document.getElementById('fish-grid');
            if(!grid) return;
            grid.innerHTML = '';
            const limit = showAllFishes ? dbFishes.length : 8;
            
            for(let i = 0; i < limit; i++) {
                if(!dbFishes[i]) continue;
                const fish = dbFishes[i];
                const isActive = fish.id === localState.fishMode;
                
                const cardClasses = isActive 
                    ? 'bg-blue-50 border-blue-200 shadow-lg transform -translate-y-1' 
                    : 'bg-white border-slate-100 shadow-sm hover:shadow-md';
                    
                const iconBgClasses = isActive 
                    ? 'bg-gradient-to-br from-blue-400 to-indigo-500 text-white shadow-md' 
                    : 'bg-slate-50 text-slate-400 shadow-sm';
                    
                const iconColor = isActive ? 'text-white' : (fish.color || 'text-slate-400');
                const textColor = isActive ? 'text-blue-800' : 'text-slate-600';
                const fIcon = fish.icon || 'fa-fish';
                
                const html = `
                    <div onclick="requireAuth(() => selectHomeFish(${fish.id}))" class="has-ripple smooth-transition rounded-[1.25rem] p-3 border relative overflow-hidden cursor-pointer active:scale-95 ${cardClasses}">
                        <div class="flex flex-col items-center justify-center gap-2 relative z-10 pointer-events-none">
                            <div class="smooth-transition w-11 h-11 rounded-2xl flex items-center justify-center ${iconBgClasses}">
                                <i class="fa-solid ${fIcon} ${iconColor} text-base smooth-transition"></i>
                            </div>
                            <span class="text-[11px] font-bold ${textColor} text-center leading-tight smooth-transition">${fish.name}</span>
                        </div>
                    </div>
                `;
                grid.innerHTML += html;
            }
            
            const btn = document.getElementById('toggle-fish-btn');
            if(btn) {
                if(dbFishes.length > 8) {
                    btn.classList.remove('hidden');
                    btn.innerText = showAllFishes ? 'Hide List' : 'See More';
                } else {
                    btn.classList.add('hidden');
                }
            }
        }

        window.toggleFishList = function() {
            showAllFishes = !showAllFishes;
            renderHomeFishes();
        }

                window.selectHomeFish = function(id) {
    showLoading();
    const fish = dbFishes.find(f => f.id === id);
    if (!fish) return;
    
    let updates = {};
    updates['/fishMode'] = id;
    updates['/currentFishName'] = fish.name;
    
    if (id !== 0) {
        updates['/targetPhMin'] = fish.phMin || 0;
        updates['/targetPhMax'] = fish.phMax || 0;
        updates['/targetTempMin'] = fish.tempMin || 0;
        updates['/targetTempMax'] = fish.tempMax || 0;
        updates['/REL_WATER_FLOW'] = fish.flow || false;
        updates['/REL_RAIN_PUMP'] = fish.rain || false;
        updates['/REL_AIR_PUMP'] = true;
    } else {
        updates['/REL_AIR_PUMP'] = false;
    }
    
    update(ref(db), updates).then(() => {
        setTimeout(hideLoading, 300);
    });
}

window.saveFishData = function() {
    const name = document.getElementById('input-fish-name').value;
    const phMin = parseFloat(document.getElementById('input-ph-min').value);
    const phMax = parseFloat(document.getElementById('input-ph-max').value);
    const tempMin = parseFloat(document.getElementById('input-temp-min').value);
    const tempMax = parseFloat(document.getElementById('input-temp-max').value);
    
    if (!name) return;
    
    let updatedFishes = [...dbFishes];
    let updates = {};
    
    if (editId) {
        const idx = updatedFishes.findIndex(x => x.id === editId);
        const existingColor = updatedFishes[idx].color;
        const existingIcon = updatedFishes[idx].icon;
        updatedFishes[idx] = { id: editId, name, icon: existingIcon, phMin, phMax, tempMin, tempMax, flow: inputFlowVal, rain: inputRainVal, color: existingColor };
        
        if (editId === localState.fishMode) {
            updates['/targetPhMin'] = phMin || 0;
            updates['/targetPhMax'] = phMax || 0;
            updates['/targetTempMin'] = tempMin || 0;
            updates['/targetTempMax'] = tempMax || 0;
            updates['/REL_WATER_FLOW'] = inputFlowVal || false;
            updates['/REL_RAIN_PUMP'] = inputRainVal || false;
            updates['/currentFishName'] = name;
        }
    } else {
        const newId = updatedFishes.length ? Math.max(...updatedFishes.map(x => x.id)) + 1 : 1;
        const randomColor = fishColors[Math.floor(Math.random() * fishColors.length)];
        updatedFishes.push({ id: newId, name, icon: 'fa-fish', phMin, phMax, tempMin, tempMax, flow: inputFlowVal, rain: inputRainVal, color: randomColor });
        updates['/lastAddedFish'] = name;
    }
    
    updates['/fishes'] = updatedFishes;
    
    update(ref(db), updates).then(() => {
        if (!editId) {
            setTimeout(() => { set(ref(db, 'lastAddedFish'), ""); }, 6000);
        }
        document.getElementById('search-fish-input').value = '';
        closeFishModal();
    });
}
        function updateCurrentModeUI() {
            const fish = dbFishes.find(f => f.id === localState.fishMode) || dbFishes[0];
            if(fish) {
                document.getElementById('current-mode-text').innerText = fish.name + ' Mode';
                const modeIcon = document.getElementById('current-mode-icon');
                modeIcon.className = `fa-solid ${fish.icon || 'fa-fish'} text-xl smooth-transition ${fish.color || 'text-slate-400'}`;
            }
        }
        
        window.toggleCamera = function() {
            isCamVisible = !isCamVisible;
            const container = document.getElementById('camera-container');
            const btn = document.getElementById('toggle-cam-btn');
            if(isCamVisible) {
                container.classList.remove('hidden');
                setTimeout(() => {
                    container.classList.remove('opacity-0', 'scale-95');
                    container.classList.add('opacity-100', 'scale-100');
                }, 10);
                btn.innerText = 'Hide';
                btn.classList.add('bg-teal-50', 'text-teal-600', 'border', 'border-teal-100');
                btn.classList.remove('bg-slate-100', 'text-slate-600');
            } else {
                container.classList.add('opacity-0', 'scale-95');
                container.classList.remove('opacity-100', 'scale-100');
                setTimeout(() => {
                    container.classList.add('hidden');
                }, 300);
                btn.innerText = 'Show';
                btn.classList.remove('bg-teal-50', 'text-teal-600', 'border', 'border-teal-100');
                btn.classList.add('bg-slate-100', 'text-slate-600');
            }
        }
        
        window.moveCamServo = function(axis, step) {
            if(axis === 'pan') {
                camPan += step;
                if(camPan > 180) camPan = 180;
                if(camPan < 0) camPan = 0;
                set(ref(db, 'camera/pan'), camPan);
            } else {
                camTilt += step;
                if(camTilt > 180) camTilt = 180;
                if(camTilt < 0) camTilt = 0;
                set(ref(db, 'camera/tilt'), camTilt);
            }
        }

        window.initRelays = function() {
            const container = document.getElementById('relay-container');
            if(!container) return;
            container.innerHTML = ''; 
            relaysMeta.forEach((relay) => {
                const html = `
                    <div id="card-${relay.id}" class="has-ripple smooth-transition bg-white rounded-[1.25rem] p-4 shadow-sm border border-slate-100 relative overflow-hidden cursor-pointer hover:shadow-md active:scale-95" onclick="requireAuth(() => toggleRelay('${relay.id}'))">
                        <div class="flex justify-between items-start z-10 relative pointer-events-none">
                            <div id="icon-bg-${relay.id}" class="smooth-transition w-11 h-11 rounded-2xl bg-slate-50 flex items-center justify-center shadow-sm">
                                <i id="icon-${relay.id}" class="fa-solid ${relay.icon} text-base text-slate-400 smooth-transition"></i>
                            </div>
                            <div class="relative inline-block w-10 align-middle select-none">
                                <div class="w-10 h-5 bg-slate-200 rounded-full shadow-inner smooth-transition" id="track-${relay.id}"></div>
                                <div class="toggle-switch absolute top-0.5 left-0.5 w-4 h-4 bg-white rounded-full shadow-md" id="knob-${relay.id}"></div>
                            </div>
                        </div>
                        <div class="mt-4 relative z-10 pointer-events-none">
                            <h3 id="title-${relay.id}" class="font-bold text-slate-600 text-sm mb-1 smooth-transition">${relay.name}</h3>
                            <p id="status-${relay.id}" class="text-[10px] font-bold tracking-wider text-slate-400 smooth-transition">OFF</p>
                        </div>
                    </div>
                `;
                container.innerHTML += html;
            });
            updateRelayVisuals();
        }

        function updateRelayVisuals() {
            relaysMeta.forEach((relay) => {
                const isOn = relayStates[relay.id];
                const card = document.getElementById(`card-${relay.id}`);
                if(!card) return;
                const iconBg = document.getElementById(`icon-bg-${relay.id}`);
                const icon = document.getElementById(`icon-${relay.id}`);
                const track = document.getElementById(`track-${relay.id}`);
                const knob = document.getElementById(`knob-${relay.id}`);
                const title = document.getElementById(`title-${relay.id}`);
                const status = document.getElementById(`status-${relay.id}`);

                if (isOn) {
                    card.className = `has-ripple smooth-transition rounded-[1.25rem] p-4 shadow-md border relative overflow-hidden cursor-pointer hover:shadow-lg active:scale-95 ${relay.activeBg}`;
                    iconBg.className = `smooth-transition w-11 h-11 rounded-2xl bg-gradient-to-br ${relay.color} flex items-center justify-center shadow-md text-white`;
                    icon.className = `fa-solid ${relay.icon} text-base text-white smooth-transition`;
                    track.className = "w-10 h-5 bg-blue-500 rounded-full shadow-inner smooth-transition";
                    knob.style.transform = "translateX(20px)";
                    title.className = "font-bold text-slate-800 text-sm mb-1 smooth-transition";
                    status.innerText = "ACTIVE";
                    status.className = "text-[10px] font-bold tracking-wider text-blue-600 smooth-transition";
                } else {
                    card.className = "has-ripple smooth-transition bg-white rounded-[1.25rem] p-4 shadow-sm border border-slate-100 relative overflow-hidden cursor-pointer hover:shadow-md active:scale-95";
                    iconBg.className = "smooth-transition w-11 h-11 rounded-2xl bg-slate-50 flex items-center justify-center shadow-sm";
                    icon.className = `fa-solid ${relay.icon} text-base text-slate-400 smooth-transition`;
                    track.className = "w-10 h-5 bg-slate-200 rounded-full shadow-inner smooth-transition";
                    knob.style.transform = "translateX(0px)";
                    title.className = "font-bold text-slate-600 text-sm mb-1 smooth-transition";
                    status.innerText = "OFF";
                    status.className = "text-[10px] font-bold tracking-wider text-slate-400 smooth-transition";
                }
            });
        }

        window.toggleRelay = function(id) {
            showLoading();
            set(ref(db, id), !relayStates[id]).then(() => {
                setTimeout(hideLoading, 300);
            });
        }

        window.renderDbFishes = function(fishesToRender = dbFishes) {
            const list = document.getElementById('custom-fish-list');
            if(!list) return;
            list.innerHTML = '';
            
            const realFishes = fishesToRender.filter(f => f.id !== 0);
            
            if(realFishes.length === 0) {
                list.innerHTML = `<div class="text-center py-10 opacity-50"><i class="fa-solid fa-fish text-4xl mb-3"></i><p>No fish data found</p></div>`;
                return;
            }

            realFishes.forEach(f => {
                const flowIcon = f.flow ? '<i class="fa-solid fa-water text-blue-500"></i>' : '<i class="fa-solid fa-water text-slate-300"></i>';
                const rainIcon = f.rain ? '<i class="fa-solid fa-cloud-rain text-indigo-500"></i>' : '<i class="fa-solid fa-cloud-rain text-slate-300"></i>';
                const fIcon = f.icon || 'fa-fish';
                
                const html = `
                <div class="bg-white rounded-[1.5rem] p-5 border border-slate-100 shadow-sm flex items-center gap-4">
                    <div class="w-14 h-14 rounded-[1.25rem] bg-slate-50 flex items-center justify-center flex-shrink-0 shadow-inner">
                        <i class="fa-solid ${fIcon} ${f.color} text-2xl"></i>
                    </div>
                    <div class="flex-1">
                        <h3 class="font-bold text-slate-800 text-lg mb-1.5">${f.name}</h3>
                        <div class="flex gap-2 mb-2.5">
                            <span class="bg-blue-50 text-blue-600 px-2 py-0.5 rounded-lg text-[10px] font-bold tracking-wide border border-blue-100">pH ${f.phMin || 0}-${f.phMax || 0}</span>
                            <span class="bg-rose-50 text-rose-600 px-2 py-0.5 rounded-lg text-[10px] font-bold tracking-wide border border-rose-100">${f.tempMin || 0}-${f.tempMax || 0}°C</span>
                        </div>
                        <div class="flex gap-3 text-base">
                            ${flowIcon}
                            ${rainIcon}
                        </div>
                    </div>
                    <div class="flex flex-col gap-2">
                        <button onclick="requireAuth(() => editFish(${f.id}))" class="w-10 h-10 rounded-xl bg-blue-50 text-blue-500 flex items-center justify-center active:scale-90 transition-all">
                            <i class="fa-solid fa-pen text-sm"></i>
                        </button>
                        <button onclick="requireAuth(() => reqDeleteFish(${f.id}))" class="w-10 h-10 rounded-xl bg-rose-50 text-rose-500 flex items-center justify-center active:scale-90 transition-all">
                            <i class="fa-solid fa-trash text-sm"></i>
                        </button>
                    </div>
                </div>
                `;
                list.innerHTML += html;
            });
        }

        window.searchFishData = function() {
            const query = document.getElementById('search-fish-input').value.toLowerCase();
            const filteredFishes = dbFishes.filter(fish => fish.name.toLowerCase().includes(query));
            renderDbFishes(filteredFishes);
        }

        let editId = null;
        let deleteId = null;
        let inputFlowVal = false;
        let inputRainVal = false;

        window.toggleInput = function(type) {
            if(type === 'flow') {
                inputFlowVal = !inputFlowVal;
                document.getElementById('track-input-flow').className = inputFlowVal ? "w-12 h-6 bg-blue-500 rounded-full shadow-inner smooth-transition" : "w-12 h-6 bg-slate-200 rounded-full shadow-inner smooth-transition";
                document.getElementById('knob-input-flow').style.transform = inputFlowVal ? "translateX(24px)" : "translateX(0px)";
            } else {
                inputRainVal = !inputRainVal;
                document.getElementById('track-input-rain').className = inputRainVal ? "w-12 h-6 bg-indigo-500 rounded-full shadow-inner smooth-transition" : "w-12 h-6 bg-slate-200 rounded-full shadow-inner smooth-transition";
                document.getElementById('knob-input-rain').style.transform = inputRainVal ? "translateX(24px)" : "translateX(0px)";
            }
        }

        function setInputToggles(flow, rain) {
            inputFlowVal = !flow; toggleInput('flow');
            inputRainVal = !rain; toggleInput('rain');
        }

        window.openFishModal = function() {
            editId = null;
            document.getElementById('modal-title').innerText = 'Add New Fish';
            document.getElementById('input-fish-name').value = '';
            document.getElementById('input-ph-min').value = '';
            document.getElementById('input-ph-max').value = '';
            document.getElementById('input-temp-min').value = '';
            document.getElementById('input-temp-max').value = '';
            setInputToggles(false, false);
            
            document.getElementById('fish-modal').classList.remove('hidden');
            document.getElementById('fish-modal').classList.add('flex');
        }

        window.closeFishModal = function() {
            document.getElementById('fish-modal').classList.add('hidden');
            document.getElementById('fish-modal').classList.remove('flex');
        }

        window.editFish = function(id) {
            editId = id;
            const f = dbFishes.find(x => x.id === id);
            document.getElementById('modal-title').innerText = 'Edit Fish';
            document.getElementById('input-fish-name').value = f.name;
            document.getElementById('input-ph-min').value = f.phMin || '';
            document.getElementById('input-ph-max').value = f.phMax || '';
            document.getElementById('input-temp-min').value = f.tempMin || '';
            document.getElementById('input-temp-max').value = f.tempMax || '';
            setInputToggles(f.flow || false, f.rain || false);
            
            document.getElementById('fish-modal').classList.remove('hidden');
            document.getElementById('fish-modal').classList.add('flex');
        }
        window.reqDeleteFish = function(id) {
            deleteId = id;
            document.getElementById('delete-modal').classList.remove('hidden');
            document.getElementById('delete-modal').classList.add('flex');
        }

        window.closeDeleteModal = function() {
            deleteId = null;
            document.getElementById('delete-modal').classList.add('hidden');
            document.getElementById('delete-modal').classList.remove('flex');
        }

        window.confirmDelete = function() {
            if(deleteId) {
                const updatedFishes = dbFishes.filter(x => x.id !== deleteId);
                set(ref(db, 'fishes'), updatedFishes);
                document.getElementById('search-fish-input').value = '';
            }
            closeDeleteModal();
        }

        window.switchPage = function(page) {
            const pages = ['home-page', 'data-page', 'settings-page'];
            pages.forEach(p => {
                const el = document.getElementById(p);
                el.classList.add('hidden');
                el.classList.remove('block', 'page-enter');
            });
            
            const activePage = document.getElementById(page);
            activePage.classList.remove('hidden');
            activePage.classList.add('block', 'page-enter');
            
            setTimeout(() => {
                activePage.classList.remove('page-enter');
            }, 400);
            
            const header = document.getElementById('main-header');
            if (page === 'home-page') {
                header.classList.remove('-translate-y-full', 'opacity-0');
            } else {
                header.classList.add('-translate-y-full', 'opacity-0');
            }

            document.querySelectorAll('.nav-btn').forEach(btn => {
                btn.classList.remove('text-blue-600', 'bg-blue-50');
                btn.classList.add('text-slate-400');
            });
            const activeBtn = document.getElementById('nav-' + page);
            activeBtn.classList.remove('text-slate-400');
            activeBtn.classList.add('text-blue-600', 'bg-blue-50');
            window.scrollTo({ top: 0, behavior: 'smooth' });
        }

        let deferredPrompt;
        window.addEventListener('beforeinstallprompt', (e) => {
            e.preventDefault();
            deferredPrompt = e;
            const btn = document.getElementById('install-app-btn');
            const hint = document.getElementById('install-hint');
            if(btn && hint) {
                btn.classList.remove('hidden');
                hint.classList.add('hidden');
            }
        });

        window.installPWA = async function() {
            if (deferredPrompt) {
                deferredPrompt.prompt();
                const { outcome } = await deferredPrompt.userChoice;
                if (outcome === 'accepted') {
                    document.getElementById('install-app-btn').classList.add('hidden');
                    document.getElementById('install-hint').classList.remove('hidden');
                    document.getElementById('install-hint').innerText = 'Installed Successfully!';
                }
                deferredPrompt = null;
            } else {
                alert('App is already installed or your browser does not support this feature yet.');
            }
        }

        document.addEventListener('click', function (e) {
            const target = e.target.closest('.has-ripple');
            if (!target) return;
            const circle = document.createElement('span');
            const diameter = Math.max(target.clientWidth, target.clientHeight);
            const radius = diameter / 2;
            const rect = target.getBoundingClientRect();
            circle.style.width = circle.style.height = `${diameter}px`;
            circle.style.left = `${e.clientX - rect.left - radius}px`;
            circle.style.top = `${e.clientY - rect.top - radius}px`;
            circle.classList.add('ripple-span');
            const existingRipple = target.querySelector('.ripple-span');
            if (existingRipple) {
                existingRipple.remove();
            }
            target.appendChild(circle);
            setTimeout(() => {
                circle.remove();
            }, 600);
        });

        window.showAlert = function(title, message, type) {
            const customAlertModal = document.getElementById('custom-alert-modal');
            const alertTitle = document.getElementById('alert-title');
            const alertMessage = document.getElementById('alert-message');
            const alertIconContainer = document.getElementById('alert-icon-container');
            const alertIcon = document.getElementById('alert-icon');
            const alertBtn = document.getElementById('alert-btn');

            alertTitle.innerText = title;
            alertMessage.innerText = message;
            
            alertIcon.className = "fa-solid";
            alertIconContainer.className = "w-16 h-16 rounded-2xl flex items-center justify-center mx-auto mb-4 text-2xl border";
            
            if(type === "error") {
                alertIcon.classList.add("fa-triangle-exclamation");
                alertIconContainer.classList.add("bg-orange-50", "text-orange-500", "border-orange-100");
                alertBtn.className = "has-ripple w-full py-3 rounded-xl bg-gradient-to-r from-orange-400 to-orange-500 text-white font-bold text-sm shadow-md transition-all active:scale-95";
            } else if(type === "success") {
                alertIcon.classList.add("fa-check");
                alertIconContainer.classList.add("bg-green-50", "text-green-500", "border-green-100");
                alertBtn.className = "has-ripple w-full py-3 rounded-xl bg-gradient-to-r from-green-400 to-green-500 text-white font-bold text-sm shadow-md transition-all active:scale-95";
            } else {
                alertIcon.classList.add("fa-circle-info");
                alertIconContainer.classList.add("bg-blue-50", "text-blue-500", "border-blue-100");
                alertBtn.className = "has-ripple w-full py-3 rounded-xl bg-gradient-to-r from-blue-400 to-blue-500 text-white font-bold text-sm shadow-md transition-all active:scale-95";
            }
            
            customAlertModal.classList.remove('hidden');
            customAlertModal.classList.add('flex');
        }

        window.closeCustomAlert = function() {
            const customAlertModal = document.getElementById('custom-alert-modal');
            customAlertModal.classList.add('hidden');
            customAlertModal.classList.remove('flex');
        }

        window.showLoading = function() {
            const loadingOverlay = document.getElementById('loading-overlay');
            loadingOverlay.classList.remove('hidden');
            loadingOverlay.classList.add('flex');
        }

        window.hideLoading = function() {
            const loadingOverlay = document.getElementById('loading-overlay');
            loadingOverlay.classList.add('hidden');
            loadingOverlay.classList.remove('flex');
        }

        window.checkPassLength = function() {
    const passInput = document.getElementById('wifi-pass');
    const errorMsg = document.getElementById('wifi-pass-error');
    if (passInput.value.length >= 8) {
        errorMsg.classList.add('hidden');
        passInput.classList.remove('border-red-400', 'focus:border-red-500', 'focus:ring-red-200');
        passInput.classList.add('border-slate-200', 'focus:border-blue-500', 'focus:ring-blue-200');
    }
}

window.saveWifiConfig = function() {
    const ssid = document.getElementById('wifi-ssid').value;
    const passInput = document.getElementById('wifi-pass');
    const pass = passInput.value;
    const errorMsg = document.getElementById('wifi-pass-error');

    if(!ssid) {
        showAlert("Invalid Input", "Please enter a valid WiFi Name.", "error");
        return;
    }

    if(pass.length < 8) {
        errorMsg.classList.remove('hidden');
        passInput.classList.remove('border-slate-200', 'focus:border-blue-500', 'focus:ring-blue-200');
        passInput.classList.add('border-red-400', 'focus:border-red-500', 'focus:ring-red-200');
        return;
    }

    showLoading();
    let updates = {};
    updates['/savedWifiConfig'] = { ssid: ssid, pass: pass };
    updates['/esp32'] = { ssid: ssid, pass: pass };
    updates['/espCam'] = { ssid: ssid, pass: pass };

    update(ref(db), updates).then(() => {
        hideLoading();
        showAlert("Successful", "WiFi details sent successfully! Devices will reboot.", "success");
        document.getElementById('wifi-ssid').value = '';
        passInput.value = '';
        errorMsg.classList.add('hidden');
        passInput.classList.remove('border-red-400', 'focus:border-red-500', 'focus:ring-red-200');
        passInput.classList.add('border-slate-200', 'focus:border-blue-500', 'focus:ring-blue-200');
    }).catch(() => {
        hideLoading();
        showAlert("Error", "Failed to send WiFi details.", "error");
    });
}

        window.onload = () => {
            initRelays();
            switchPage('home-page');
        };