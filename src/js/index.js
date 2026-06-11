var Clay = require('@rebble/clay');
var clayConfig = require('./config.json');
var clay = new Clay(clayConfig);

// Weather message keys — must match C defines
var WEATHER_TEMP_KEY = 200;
var WEATHER_COND_KEY = 201;
var WEATHER_REQ_KEY  = 202;

function xhrRequest(url, type, callback) {
    var xhr = new XMLHttpRequest();
    xhr.onload = function() { callback(this.responseText); };
    xhr.onerror = function() { console.log('XHR error for ' + url); };
    xhr.open(type, url);
    xhr.send();
}

function weatherCodeToCondition(code) {
    if (code === 0)  return 'Clear';
    if (code <= 3)   return 'Cloudy';
    if (code <= 48)  return 'Fog';
    if (code <= 55)  return 'Drizzle';
    if (code <= 65)  return 'Rain';
    if (code <= 75)  return 'Snow';
    if (code <= 82)  return 'Showers';
    if (code <= 86)  return 'Snow Showers';
    if (code >= 95)  return 'T-Storm';
    return 'Unknown';
}

function getWeather() {
    navigator.geolocation.getCurrentPosition(
        function(pos) {
            var url = 'https://api.open-meteo.com/v1/forecast' +
                '?latitude='  + pos.coords.latitude +
                '&longitude=' + pos.coords.longitude +
                '&current=temperature_2m,weather_code';

            xhrRequest(url, 'GET', function(body) {
                try {
                    var json = JSON.parse(body);
                    var temp = Math.round(json.current.temperature_2m);
                    var cond = weatherCodeToCondition(json.current.weather_code);
                    var dict = {};
                    dict[WEATHER_TEMP_KEY] = temp;
                    dict[WEATHER_COND_KEY] = cond;
                    Pebble.sendAppMessage(dict,
                        function()  { console.log('Weather sent OK: ' + temp + 'C ' + cond); },
                        function(e) { console.log('Weather send failed: ' + JSON.stringify(e)); }
                    );
                } catch(e) {
                    console.log('Weather parse error: ' + e);
                }
            });
        },
        function(err) {
            console.log('Geolocation error: ' + err.message);
        },
        { timeout: 15000, maximumAge: 300000 }
    );
}

Pebble.addEventListener('ready', function() {
    console.log('PebbleKit JS ready — fetching weather');
    getWeather();
});

Pebble.addEventListener('appmessage', function(e) {
    if (e.payload[WEATHER_REQ_KEY]) {
        console.log('Watch requested weather refresh');
        getWeather();
    }
});
