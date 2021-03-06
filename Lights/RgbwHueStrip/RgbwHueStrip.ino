#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <NeoPixelBus.h>

#include <ESP8266WebServer.h>


const char* ssid = "MikroTik";
const char* password = "nustiuceparola";

#define lightsCount 3
#define pixelCount 30
#define startup_brightness 0
#define startup_color 0
// 0 = warm_white, 1 =  neutral, 2 = cold_white, 3 = red, 4 = green, 5 = blue

// if you want to setup static ip uncomment these 3 lines and line 72
//IPAddress strip_ip ( 192,  168,   10,  95);
//IPAddress gateway_ip ( 192,  168,   10,   1);
//IPAddress subnet_mask(255, 255, 255,   0);

uint8_t rgbw[lightsCount][4], color_mode[lightsCount];
bool light_state[lightsCount];
int transitiontime[lightsCount], ct[lightsCount], hue[lightsCount], bri[lightsCount], sat[lightsCount];
float step_level[lightsCount][4], current_rgbw[lightsCount][4], x[lightsCount], y[lightsCount];
byte mac[6];

ESP8266WebServer server(80);

RgbwColor red = RgbwColor(255, 0, 0, 0);
RgbwColor green = RgbwColor(0, 255, 0, 0);
RgbwColor white = RgbwColor(255);
RgbwColor black = RgbwColor(0);

NeoPixelBus<NeoGrbwFeature, Neo800KbpsMethod> strip(pixelCount);

void convert_hue(uint8_t light)
{
  double      hh, p, q, t, ff, s, v;
  long        i;

  rgbw[light][3] = 0;
  s = sat[light] / 255.0;
  v = bri[light] / 255.0;

  if (s <= 0.0) {      // < is bogus, just shuts up warnings
    rgbw[light][0] = v;
    rgbw[light][1] = v;
    rgbw[light][2] = v;
    return;
  }
  hh = hue[light];
  if (hh >= 65535.0) hh = 0.0;
  hh /= 11850, 0;
  i = (long)hh;
  ff = hh - i;
  p = v * (1.0 - s);
  q = v * (1.0 - (s * ff));
  t = v * (1.0 - (s * (1.0 - ff)));

  switch (i) {
    case 0:
      rgbw[light][0] = v * 255.0;
      rgbw[light][1] = t * 255.0;
      rgbw[light][2] = p * 255.0;
      break;
    case 1:
      rgbw[light][0] = q * 255.0;
      rgbw[light][1] = v * 255.0;
      rgbw[light][2] = p * 255.0;
      break;
    case 2:
      rgbw[light][0] = p * 255.0;
      rgbw[light][1] = v * 255.0;
      rgbw[light][2] = t * 255.0;
      break;

    case 3:
      rgbw[light][0] = p * 255.0;
      rgbw[light][1] = q * 255.0;
      rgbw[light][2] = v * 255.0;
      break;
    case 4:
      rgbw[light][0] = t * 255.0;
      rgbw[light][1] = p * 255.0;
      rgbw[light][2] = v * 255.0;
      break;
    case 5:
    default:
      rgbw[light][0] = v * 255.0;
      rgbw[light][1] = p * 255.0;
      rgbw[light][2] = q * 255.0;
      break;
  }

}

void convert_xy(uint8_t light)
{
  float Y = bri[light] / 250.0f;

  float z = 1.0f - x[light] - y[light];

  float X = (Y / y[light]) * x[light];
  float Z = (Y / y[light]) * z;

  // sRGB D65 conversion
  float r =  X * 1.656492f - Y * 0.354851f - Z * 0.255038f;
  float g = -X * 0.707196f + Y * 1.655397f + Z * 0.036152f;
  float b =  X * 0.051713f - Y * 0.121364f + Z * 1.011530f;

  if (r > b && r > g && r > 1.0f) {
    // red is too big
    g = g / r;
    b = b / r;
    r = 1.0f;
  }
  else if (g > b && g > r && g > 1.0f) {
    // green is too big
    r = r / g;
    b = b / g;
    g = 1.0f;
  }
  else if (b > r && b > g && b > 1.0f) {
    // blue is too big
    r = r / b;
    g = g / b;
    b = 1.0f;
  }

  // Apply gamma correction
  r = r <= 0.0031308f ? 12.92f * r : (1.0f + 0.055f) * pow(r, (1.0f / 2.4f)) - 0.055f;
  g = g <= 0.0031308f ? 12.92f * g : (1.0f + 0.055f) * pow(g, (1.0f / 2.4f)) - 0.055f;
  b = b <= 0.0031308f ? 12.92f * b : (1.0f + 0.055f) * pow(b, (1.0f / 2.4f)) - 0.055f;

  if (r > b && r > g) {
    // red is biggest
    if (r > 1.0f) {
      g = g / r;
      b = b / r;
      r = 1.0f;
    }
  }
  else if (g > b && g > r) {
    // green is biggest
    if (g > 1.0f) {
      r = r / g;
      b = b / g;
      g = 1.0f;
    }
  }
  else if (b > r && b > g) {
    // blue is biggest
    if (b > 1.0f) {
      r = r / b;
      g = g / b;
      b = 1.0f;
    }
  }

  r = r < 0 ? 0 : r;
  g = g < 0 ? 0 : g;
  b = b < 0 ? 0 : b;

  rgbw[light][0] = (int) (r * 255.0f); rgbw[light][1] = (int) (g * 255.0f); rgbw[light][2] = (int) (b * 255.0f); rgbw[light][3] = 0;
}

void convert_ct(uint8_t light) {
  int hectemp = 10000 / ct[light];
  int r, g, b;
  if (hectemp <= 66) {
    r = 255;
    g = 99.4708025861 * log(hectemp) - 161.1195681661;
    b = hectemp <= 19 ? 0 : (138.5177312231 * log(hectemp - 10) - 305.0447927307);
  } else {
    r = 329.698727446 * pow(hectemp - 60, -0.1332047592);
    g = 288.1221695283 * pow(hectemp - 60, -0.0755148492);
    b = 255;
  }
  r = r > 255 ? 255 : r;
  g = g > 255 ? 255 : g;
  b = b > 255 ? 255 : b;
  rgbw[light][0] = r * (bri[light] / 255.0f); rgbw[light][1] = g * (bri[light] / 255.0f); rgbw[light][2] = b * (bri[light] / 255.0f); rgbw[light][3] = bri[light];
}

void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void infoLight(RgbwColor color) {
  // Flash the strip in the selected color. White = booted, green = WLAN connected, red = WLAN could not connect
  for (int i = 0; i < pixelCount; i++)
  {
    strip.SetPixelColor(i, color);
    strip.Show();
    delay(10);
    strip.SetPixelColor(i, black);
    strip.Show();
  }
}

void setup() {
  strip.Begin();
  strip.Show();

  // Show that the NeoPixels are alive
  delay(120); // Apparently needed to make the first few pixels animate correctly

  //WiFi.config(strip_ip, gateway_ip, subnet_mask);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  if (startup_brightness == 0) {
    infoLight(white);

    while (WiFi.status() != WL_CONNECTED) {
      infoLight(red);
      delay(500);
    }
    // Show that we are connected
    infoLight(green);

    //setup default warm_white on power on
    for (int i = 0; i < lightsCount; i++) {
      rgbw[i][0] = 254; rgbw[i][1] = 145; rgbw[i][2] = 40;
    }

  } else {
    //setup start color/brightness and transitiontime
    for (int i = 0; i < lightsCount; i++) {
      bri[i] = startup_brightness;
      //setup start color/brightness and transitiontime
      if ( startup_color == 0) {
        ct[i] = 400; convert_ct(i);
      } else if ( startup_color == 1) {
        ct[i] = 320; convert_ct(i);
      } else if ( startup_color == 2) {
        ct[i] = 200; convert_ct(i);
      }  else if ( startup_color == 3) {
        rgbw[i][0] = 254.0f * (startup_brightness / 255.0f); rgbw[i][1] = 0; rgbw[i][2] = 0;
      }  else if ( startup_color == 4) {
        rgbw[i][0] = 0; rgbw[i][1] = 254.0f * (startup_brightness / 255.0f); rgbw[i][2] = 0;
      }  else if ( startup_color == 5) {
        rgbw[i][0] = 0; rgbw[i][1] = 0; rgbw[i][2] = 254.0f * (startup_brightness / 255.0f);
      }
      step_level[i][0] = rgbw[i][0] / 350.0f; step_level[i][1] = rgbw[i][1] / 350.0f; step_level[i][2] = rgbw[i][2] / 350.0f; step_level[i][3] = rgbw[i][3] / 350.0f;
      light_state[i] = true;
    }
  }

  WiFi.macAddress(mac);

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  // ArduinoOTA.setHostname("myesp8266");

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.begin();

  pinMode(LED_BUILTIN, OUTPUT);     // Initialize the LED_BUILTIN pin as an output
  digitalWrite(LED_BUILTIN, HIGH);  // Turn the LED off by making the voltage HIGH


  server.on("/set", []() {
    uint8_t light;
    float transitiontime = 40;
    for (uint8_t i = 0; i < server.args(); i++) {
      if (server.argName(i) == "light") {
        light = server.arg(i).toInt() - 1;
        light_state[light] = true;
      }
      else if (server.argName(i) == "on") {
        if (server.arg(i) == "1") {
          light_state[light] = true;
        }
        else {
          light_state[light] = false;
        }
      }
      else if (server.argName(i) == "r") {
        rgbw[light][0] = server.arg(i).toInt();
        color_mode[light] = 0;
      }
      else if (server.argName(i) == "g") {
        rgbw[light][1] = server.arg(i).toInt();
        color_mode[light] = 0;
      }
      else if (server.argName(i) == "b") {
        rgbw[light][2] = server.arg(i).toInt();
        color_mode[light] = 0;
      }
      else if (server.argName(i) == "w") {
        rgbw[light][3] = server.arg(i).toInt();
        color_mode[light] = 0;
      }
      else if (server.argName(i) == "x") {
        x[light] = server.arg(i).toFloat();
        color_mode[light] = 1;
      }
      else if (server.argName(i) == "y") {
        y[light] = server.arg(i).toFloat();
        color_mode[light] = 1;
      }
      else if (server.argName(i) == "bri") {
        if (server.arg(i).toInt() == 0)
          light_state[light] = true;
        else
          bri[light] = server.arg(i).toInt();
      }
      else if (server.argName(i) == "bri_inc") {
        bri[light] += server.arg(i).toInt();
        if (bri[light] > 255) bri[light] = 255;
        else if (bri[light] < 0) bri[light] = 0;
      }
      else if (server.argName(i) == "ct") {
        ct[light] = server.arg(i).toInt();
        color_mode[light] = 2;
      }
      else if (server.argName(i) == "sat") {
        sat[light] = server.arg(i).toInt();
        color_mode[light] = 3;
      }
      else if (server.argName(i) == "hue") {
        hue[light] = server.arg(i).toInt();
        color_mode[light] = 3;
      }
      else if (server.argName(i) == "transitiontime") {
        transitiontime = server.arg(i).toInt() * 30;
      }
    }
    server.send(200, "text/plain", "OK, x: " + (String)x[light] + ", y:" + (String)y[light] + ", bri:" + (String)bri[light] + ", ct:" + ct[light] + ", colormode:" + color_mode[light] + ", state:" + light_state[light]);
    if (color_mode[light] == 1 && light_state[light] == true) {
      convert_xy(light);
    } else if (color_mode[light] == 2 && light_state[light] == true) {
      convert_ct(light);
    } else if (color_mode[light] == 3 && light_state[light] == true) {
      convert_hue(light);
    }
    if (light_state[light]) {
      step_level[light][0] = ((float)rgbw[light][0] - current_rgbw[light][0]) / transitiontime;
      step_level[light][1] = ((float)rgbw[light][1] - current_rgbw[light][1]) / transitiontime;
      step_level[light][2] = ((float)rgbw[light][2] - current_rgbw[light][2]) / transitiontime;
      step_level[light][3] = ((float)rgbw[light][3] - current_rgbw[light][3]) / transitiontime;
    } else {
      step_level[light][0] = current_rgbw[light][0] / transitiontime;
      step_level[light][1] = current_rgbw[light][1] / transitiontime;
      step_level[light][2] = current_rgbw[light][2] / transitiontime;
      step_level[light][3] = current_rgbw[light][3] / transitiontime;
    }
  });

  server.on("/get", []() {
    uint8_t light;
    for (uint8_t i = 0; i < server.args(); i++) {
      if (server.argName(i) == "light") {
        light = server.arg(i).toInt() - 1;
      }
    }
    server.send(200, "text/plain", "{\"R\":" + (String)current_rgbw[light][0] + ", \"G\": " + (String)current_rgbw[light][1] + ", \"B\":" + (String)current_rgbw[light][2] + ", \"W\":" + (String)current_rgbw[light][3] + ", \"bri\":" + (String)bri[light] + ", \"xy\": [" + (String)x[light] + "," + (String)y[light] + "], \"ct\":" + (String)ct[light] + ", \"sat\": " + (String)sat[light] + ", \"hue\": " + (String)hue[light] + ", \"colormode\":" + color_mode[light] + "}");
  });

  server.on("/detect", []() {
    server.send(200, "text/plain", "{\"hue\": \"strip\",\"lights\": " + (String)lightsCount + ",\"type\": \"rgb\",\"mac\": \"" + String(mac[5], HEX) + ":"  + String(mac[4], HEX) + ":" + String(mac[3], HEX) + ":" + String(mac[2], HEX) + ":" + String(mac[1], HEX) + ":" + String(mac[0], HEX) + "\"}");
  });

  server.on("/reset", []() {
    server.send(200, "text/plain", "reset");
    ESP.reset();
  });

  server.onNotFound(handleNotFound);

  server.begin();
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  lightEngine();
}

void lightEngine() {
  for (int i = 0; i < lightsCount; i++) {
    if (light_state[i]) {
      if (rgbw[i][0] != current_rgbw[i][0] || rgbw[i][1] != current_rgbw[i][1] || rgbw[i][2] != current_rgbw[i][2] || rgbw[i][3] != current_rgbw[i][3]) {
        if (rgbw[i][0] != current_rgbw[i][0]) current_rgbw[i][0] += step_level[i][0];
        if (rgbw[i][1] != current_rgbw[i][1]) current_rgbw[i][1] += step_level[i][1];
        if (rgbw[i][2] != current_rgbw[i][2]) current_rgbw[i][2] += step_level[i][2];
        if (rgbw[i][3] != current_rgbw[i][3]) current_rgbw[i][3] += step_level[i][3];
        if ((step_level[i][0] > 0.0 && current_rgbw[i][0] > rgbw[i][0]) || (step_level[i][0] < 0.0 && current_rgbw[i][0] < rgbw[i][0])) current_rgbw[i][0] = rgbw[i][0];
        if ((step_level[i][1] > 0.0 && current_rgbw[i][1] > rgbw[i][1]) || (step_level[i][1] < 0.0 && current_rgbw[i][1] < rgbw[i][1])) current_rgbw[i][1] = rgbw[i][1];
        if ((step_level[i][2] > 0.0 && current_rgbw[i][2] > rgbw[i][2]) || (step_level[i][2] < 0.0 && current_rgbw[i][2] < rgbw[i][2])) current_rgbw[i][2] = rgbw[i][2];
        if ((step_level[i][3] > 0.0 && current_rgbw[i][3] > rgbw[i][3]) || (step_level[i][3] < 0.0 && current_rgbw[i][3] < rgbw[i][3])) current_rgbw[i][3] = rgbw[i][3];
        for (int j = 0; j < pixelCount / lightsCount ; j++)
        {
          strip.SetPixelColor(j + i * pixelCount / lightsCount, RgbwColor((int)current_rgbw[i][0], (int)current_rgbw[i][1], (int)current_rgbw[i][2], (int)current_rgbw[i][3]));
        }
        strip.Show();
      }
    } else {
      if (current_rgbw[i][0] != 0 || current_rgbw[i][1] != 0 || current_rgbw[i][2] != 0 || current_rgbw[i][3] != 0) {
        if (current_rgbw[i][0] != 0) current_rgbw[i][0] -= step_level[i][0];
        if (current_rgbw[i][1] != 0) current_rgbw[i][1] -= step_level[i][1];
        if (current_rgbw[i][2] != 0) current_rgbw[i][2] -= step_level[i][2];
        if (current_rgbw[i][3] != 0) current_rgbw[i][3] -= step_level[i][3];
        if (current_rgbw[i][0] < 0) current_rgbw[i][0] = 0;
        if (current_rgbw[i][1] < 0) current_rgbw[i][1] = 0;
        if (current_rgbw[i][2] < 0) current_rgbw[i][2] = 0;
        if (current_rgbw[i][3] < 0) current_rgbw[i][3] = 0;
        for (int j = 0; j < pixelCount / lightsCount ; j++)
        {
          strip.SetPixelColor(j + i * pixelCount / lightsCount, RgbwColor((int)current_rgbw[i][0], (int)current_rgbw[i][1], (int)current_rgbw[i][2], (int)current_rgbw[i][3]));
        }
        strip.Show();
      }
    }
    delay(2);
  }
}
