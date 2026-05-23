#include <SFML/Graphics.hpp>
#include <SFML/Window.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <cmath>

using namespace std;

const int MAX_PORTS = 40;
const int MAX_ROUTES = 2000;
const int MAX_WAYPOINTS = 10;
const float PORT_RADIUS = 5.f;
const float HIGHLIGHT_RADIUS = 7.f;

// ----------------- Data Structures -----------------
struct Port {
    string name;
    double lat;
    double lon;
    sf::Vector2f pixel;

    bool isPreferred = false;    // user preference
    bool isEvaluated = false;    // visited/expanded in algorithms
    bool isHub = false;          // major hub; auto-highlight
};

struct Route {
    int originIndex;
    int destinationIndex;
    string date;
    string depTime;
    string arrTime;
    int vCost;
    string company;
    bool valid;

    bool isOptimal = false;      // part of current optimal path
    bool isEvaluated = false;    // considered in algorithm
    bool isLayoverEdge = false;  // layover edge (dashed)
};

struct Waypoint {
    double lat;
    double lon;
    sf::Vector2f pixel;
};

// A journey leg for advanced route generation
struct JourneyLeg {
    int routeIndex;
};

// Ship icon traveling along a specific route (for animation)
struct ShipMarker {
    int routeIndex;
    float t;                     // 0..1 along polyline parameter
    float speed;                 // movement speed factor
};

// ----------------- Globals -----------------
Port ports[MAX_PORTS];
int portCount = 0;
Route routes[MAX_ROUTES];
int routeCount = 0;

const int WAYPOINT_COUNT = 15;
Waypoint oceanWaypoints[WAYPOINT_COUNT];

JourneyLeg currentJourney[32];
int currentJourneyLen = 0;

ShipMarker ships[64];
int shipCount = 0;

// ----------------- Helpers -----------------
int findPortIndexByName(const string& name) {
    for (int i = 0; i < portCount; ++i)
        if (ports[i].name == name) return i;
    return -1;
}

sf::Vector2f latlonToPixel(double lat, double lon, const sf::Vector2u& imgSize) {
    double x = (lon + 180.0) / 360.0 * imgSize.x;
    double y = (90.0 - lat) / 180.0 * imgSize.y;
    return sf::Vector2f(static_cast<float>(x), static_cast<float>(y));
}

void loadOceanWaypoints() {
    oceanWaypoints[0].lat = 30.0;  oceanWaypoints[0].lon = 32.0;
    oceanWaypoints[1].lat = -35.0; oceanWaypoints[1].lon = 20.0;
    oceanWaypoints[2].lat = 36.0;  oceanWaypoints[2].lon = -5.0;
    oceanWaypoints[3].lat = 9.0;   oceanWaypoints[3].lon = -80.0;
    oceanWaypoints[4].lat = 2.0;   oceanWaypoints[4].lon = 100.0;
    oceanWaypoints[5].lat = 12.0;  oceanWaypoints[5].lon = 50.0;
    oceanWaypoints[6].lat = 0.0;   oceanWaypoints[6].lon = -30.0;
    oceanWaypoints[7].lat = 0.0;   oceanWaypoints[7].lon = -150.0;
    oceanWaypoints[8].lat = -10.0; oceanWaypoints[8].lon = 80.0;
    oceanWaypoints[9].lat = 50.0;  oceanWaypoints[9].lon = -30.0;
    oceanWaypoints[10].lat = 40.0; oceanWaypoints[10].lon = -160.0;
    oceanWaypoints[11].lat = -20.0; oceanWaypoints[11].lon = 160.0;
    oceanWaypoints[12].lat = 60.0; oceanWaypoints[12].lon = -170.0;
    oceanWaypoints[13].lat = -5.0; oceanWaypoints[13].lon = 115.0;
    oceanWaypoints[14].lat = 15.0; oceanWaypoints[14].lon = 65.0;
}

void loadPorts() {
    string names[MAX_PORTS] = {
        "AbuDhabi","Alexandria","Antwerp","Athens","Busan","CapeTown","Chittagong","Colombo",
        "Copenhagen","Doha","Dubai","Dublin","Durban","Genoa","Hamburg","Helsinki","HongKong",
        "Istanbul","Jakarta","Jeddah","Karachi","Lisbon","London","LosAngeles","Manila",
        "Marseille","Melbourne","Montreal","Mumbai","NewYork","Osaka","Oslo","PortLouis",
        "Rotterdam","Shanghai","Singapore","Stockholm","Sydney","Tokyo","Vancouver"
    };

    double lats[MAX_PORTS] = {
        24.4539,31.2001,51.2194,37.9838,35.1796,-33.9249,22.3569,6.9271,
        55.6761,25.2854,25.2048,53.3498,-29.8587,44.4056,53.5511,60.1699,22.3193,
        41.0082,-6.2088,21.4858,24.8607,38.7223,51.5074,34.0522,14.5995,
        43.2965,-37.8136,45.5017,19.0760,40.7128,34.6937,59.9139,-20.1609,
        51.9244,31.2304,1.3521,59.3293,-33.8688,35.6762,49.2827
    };

    double lons[MAX_PORTS] = {
        54.3773,29.9187,4.4025,23.7275,129.0756,18.4241,91.7832,79.8612,
        12.5683,51.5310,55.2708,-6.2603,31.0218,8.9463,9.9937,24.9384,114.1694,
        28.9784,106.8456,39.1925,67.0011,-9.1393,-0.1278,-118.2437,120.9842,
        5.3698,144.9631,-73.5673,72.8777,-74.0060,135.5023,10.7522,57.5012,
        4.4777,121.4737,103.8198,18.0686,151.2093,139.6503,-123.1207
    };

    for (int i = 0; i < MAX_PORTS; ++i) {
        ports[i].name = names[i];
        ports[i].lat = lats[i];
        ports[i].lon = lons[i];
        ports[i].isPreferred = false;
        ports[i].isEvaluated = false;
        ports[i].isHub = false;
        portCount++;
    }

    // Mark some ports as major hubs for extra emphasis
    string hubs[] = { "Singapore", "Rotterdam", "Shanghai", "NewYork", "London", "HongKong" };
    int hubCount = 6;
    for (int h = 0; h < hubCount; ++h) {
        int idx = findPortIndexByName(hubs[h]);
        if (idx != -1) ports[idx].isHub = true;
    }

    cout << "Loaded " << portCount << " ports" << endl;
}

bool loadRoutes(const string& fname) {
    ifstream fin(fname.c_str());
    if (!fin.is_open()) {
        cerr << "Cannot open routes file: " << fname << endl;
        return false;
    }

    string origin, destination, date, depTime, arrTime, company;
    int vCost;
    while (fin >> origin >> destination >> date >> depTime >> arrTime >> vCost >> company) {
        if (routeCount >= MAX_ROUTES) break;
        int oi = findPortIndexByName(origin);
        int di = findPortIndexByName(destination);
        if (oi == -1 || di == -1) continue;

        routes[routeCount].originIndex = oi;
        routes[routeCount].destinationIndex = di;
        routes[routeCount].date = date;
        routes[routeCount].depTime = depTime;
        routes[routeCount].arrTime = arrTime;
        routes[routeCount].vCost = vCost;
        routes[routeCount].company = company;
        routes[routeCount].valid = true;
        routes[routeCount].isOptimal = false;
        routes[routeCount].isEvaluated = false;
        routes[routeCount].isLayoverEdge = false;
        routeCount++;
    }
    fin.close();
    cout << "Loaded " << routeCount << " routes" << endl;
    return true;
}

string intToString(int val) {
    if (val == 0) return "0";
    string result = "";
    bool negative = val < 0;
    if (negative) val = -val;
    while (val > 0) {
        result = char('0' + (val % 10)) + result;
        val /= 10;
    }
    if (negative) result = "-" + result;
    return result;
}

sf::VideoMode getDesktopMode() {
    return sf::VideoMode::getDesktopMode();
}

// ----------------- Routing Helpers -----------------
int determineOceanRoute(const Port& origin, const Port& dest) {
    double oLat = origin.lat, oLon = origin.lon;
    double dLat = dest.lat, dLon = dest.lon;

    if ((oLon < 30 && oLat > 30) && (dLon > 50 && dLat > 10)) return 1;
    if ((oLon < 30 && oLat > 30) && (dLon > 100))              return 2;
    if ((oLon < -60 && oLon > -130) && (dLon > 100))           return 3;
    if ((oLon > 0 && oLon < 40 && oLat < 0) && (dLon > 60 && dLat < 30)) return 4;
    if ((oLon < -30 && oLon > -80) && (dLon > -20 && dLon < 20))         return 5;
    if ((oLon < 20 && oLat > 30) && (dLon > 30 && dLat < 0))             return 6;
    return 0;
}

void generateOceanPath(sf::Vector2f start, sf::Vector2f end, int originIdx, int destIdx,
    sf::Vector2f* pathPoints, int& pathLength) {
    pathPoints[0] = start;
    pathLength = 1;

    int routeType = determineOceanRoute(ports[originIdx], ports[destIdx]);

    if (routeType == 1) {
        pathPoints[pathLength++] = oceanWaypoints[2].pixel;
        pathPoints[pathLength++] = oceanWaypoints[0].pixel;
    }
    else if (routeType == 2) {
        pathPoints[pathLength++] = oceanWaypoints[2].pixel;
        pathPoints[pathLength++] = oceanWaypoints[0].pixel;
        pathPoints[pathLength++] = oceanWaypoints[4].pixel;
    }
    else if (routeType == 3) {
        pathPoints[pathLength++] = oceanWaypoints[3].pixel;
        pathPoints[pathLength++] = oceanWaypoints[7].pixel;
    }
    else if (routeType == 4) {
        pathPoints[pathLength++] = oceanWaypoints[1].pixel;
        pathPoints[pathLength++] = oceanWaypoints[8].pixel;
    }
    else if (routeType == 5) {
        pathPoints[pathLength++] = oceanWaypoints[6].pixel;
    }
    else if (routeType == 6) {
        pathPoints[pathLength++] = oceanWaypoints[1].pixel;
    }
    else {
        sf::Vector2f mid = (start + end) / 2.f;
        sf::Vector2f diff = end - start;
        sf::Vector2f perp(-diff.y, diff.x);
        float len = std::sqrt(perp.x * perp.x + perp.y * perp.y);
        if (len > 0) {
            perp.x /= len;
            perp.y /= len;
            float dist = std::sqrt(diff.x * diff.x + diff.y * diff.y);
            mid += perp * (dist * 0.1f);
            pathPoints[pathLength++] = mid;
        }
    }

    pathPoints[pathLength++] = end;
}

// ----------------- Drawing Helpers -----------------
void drawCurvedSegment(sf::RenderWindow& window,
    const sf::Vector2f& start,
    const sf::Vector2f& end,
    const sf::Color& color,
    float thicknessFactor,
    bool dashed)
{
    sf::Vector2f mid = (start + end) / 2.f;
    sf::Vector2f diff = end - start;
    sf::Vector2f perp(-diff.y, diff.x);
    float len = std::sqrt(perp.x * perp.x + perp.y * perp.y);
    if (len > 0) perp = perp / len;

    float dist = std::sqrt(diff.x * diff.x + diff.y * diff.y);
    sf::Vector2f control = mid + perp * (dist * 0.08f);

    int segments = 18;
    sf::Vertex line[2];

    for (int j = 0; j < segments; ++j) {
        if (dashed && (j % 2 == 1)) continue;

        float t1 = static_cast<float>(j) / segments;
        float t2 = static_cast<float>(j + 1) / segments;

        float u1 = 1.f - t1;
        sf::Vector2f p1 = u1 * u1 * start + 2.f * u1 * t1 * control + t1 * t1 * end;

        float u2 = 1.f - t2;
        sf::Vector2f p2 = u2 * u2 * start + 2.f * u2 * t2 * control + t2 * t2 * end;

        int thicknessSteps = static_cast<int>(2 * thicknessFactor);
        if (thicknessSteps < 1) thicknessSteps = 1;

        sf::Vector2f d = p2 - p1;
        float dLen = std::sqrt(d.x * d.x + d.y * d.y);
        sf::Vector2f n(0.f, 0.f);
        if (dLen > 0.f) n = sf::Vector2f(-d.y / dLen, d.x / dLen);

        for (int k = -thicknessSteps; k <= thicknessSteps; ++k) {
            float offsetScale = static_cast<float>(k) * 0.35f * thicknessFactor;
            sf::Vector2f offset = n * offsetScale;

            line[0].position = p1 + offset;
            line[1].position = p2 + offset;
            line[0].color = color;
            line[1].color = color;
            window.draw(line, 2, sf::Lines);
        }
    }
}

void drawOceanRoute(sf::RenderWindow& window,
    sf::Vector2f* points,
    int pointCount,
    const sf::Color& baseColor,
    float thicknessFactor,
    bool dashed)
{
    if (pointCount < 2) return;
    for (int i = 0; i < pointCount - 1; ++i) {
        drawCurvedSegment(window, points[i], points[i + 1], baseColor, thicknessFactor, dashed);
    }
}

void drawArrowHead(sf::RenderWindow& window,
    const sf::Vector2f& tail,
    const sf::Vector2f& head,
    const sf::Color& color)
{
    sf::Vector2f dir = head - tail;
    float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    if (len < 1.f) return;
    dir /= len;

    float arrowSize = 7.f;
    sf::Vector2f left(-dir.y, dir.x);
    sf::Vector2f p1 = head;
    sf::Vector2f p2 = head - dir * arrowSize + left * (arrowSize * 0.6f);
    sf::Vector2f p3 = head - dir * arrowSize - left * (arrowSize * 0.6f);

    sf::ConvexShape tri;
    tri.setPointCount(3);
    tri.setPoint(0, p1);
    tri.setPoint(1, p2);
    tri.setPoint(2, p3);
    tri.setFillColor(color);
    window.draw(tri);
}

void drawPortDropdown(sf::RenderWindow& window, sf::Font& font, int portIndex, sf::Vector2i mousePos) {
    int outgoingCount = 0;
    for (int i = 0; i < routeCount; ++i) {
        if (routes[i].valid && routes[i].originIndex == portIndex) {
            outgoingCount++;
        }
    }

    float dropdownWidth = 340.f;
    float lineHeight = 18.f;
    float headerHeight = 24.f;
    int maxVisibleRoutes = 8;
    int visibleRoutes = (outgoingCount < maxVisibleRoutes) ? outgoingCount : maxVisibleRoutes;
    float dropdownHeight = headerHeight + (visibleRoutes + 2) * lineHeight + 16.f;

    sf::RectangleShape shadow(sf::Vector2f(dropdownWidth + 6.f, dropdownHeight + 6.f));
    shadow.setPosition(mousePos.x + 13, mousePos.y + 13);
    shadow.setFillColor(sf::Color(0, 0, 0, 90));
    window.draw(shadow);

    sf::RectangleShape dropdown(sf::Vector2f(dropdownWidth, dropdownHeight));
    dropdown.setPosition(mousePos.x + 15, mousePos.y + 15);
    dropdown.setFillColor(sf::Color(12, 18, 28, 240));
    dropdown.setOutlineThickness(2.f);
    dropdown.setOutlineColor(sf::Color(255, 200, 0));
    window.draw(dropdown);

    sf::Text header("", font, 15);
    string h = ports[portIndex].name;
    if (ports[portIndex].isPreferred) h += "  (Preferred)";
    if (ports[portIndex].isHub) h += "  [Hub]";
    header.setString(h);
    header.setFillColor(sf::Color(255, 220, 60));
    header.setStyle(sf::Text::Bold);
    header.setPosition(mousePos.x + 25, mousePos.y + 20);
    window.draw(header);

    sf::Text coords("", font, 12);
    coords.setString("Lat: " + intToString(static_cast<int>(ports[portIndex].lat)) +
        ", Lon: " + intToString(static_cast<int>(ports[portIndex].lon)));
    coords.setFillColor(sf::Color(210, 210, 210));
    coords.setPosition(mousePos.x + 25, mousePos.y + 42);
    window.draw(coords);

    sf::Text status("", font, 12);
    string st = "Status: ";
    if (ports[portIndex].isEvaluated) st += "Evaluated node   ";
    else st += "Not evaluated   ";
    st += ports[portIndex].isHub ? "Hub port" : "Standard port";
    status.setString(st);
    status.setFillColor(sf::Color(160, 220, 255));
    status.setPosition(mousePos.x + 25, mousePos.y + 60);
    window.draw(status);

    sf::Text routesHeader("", font, 13);
    routesHeader.setString("Outgoing Routes (" + intToString(outgoingCount) + "):");
    routesHeader.setFillColor(sf::Color(150, 220, 255));
    routesHeader.setStyle(sf::Text::Bold);
    routesHeader.setPosition(mousePos.x + 25, mousePos.y + 80);
    window.draw(routesHeader);

    int displayCount = 0;
    float yOffset = 98.f;

    for (int i = 0; i < routeCount && displayCount < maxVisibleRoutes; ++i) {
        if (routes[i].valid && routes[i].originIndex == portIndex) {
            sf::Text routeInfo("", font, 11);
            string routeText = "-> " + ports[routes[i].destinationIndex].name +
                " | " + routes[i].date +
                " | " + routes[i].depTime +
                " | $" + intToString(routes[i].vCost);
            routeInfo.setString(routeText);
            routeInfo.setFillColor(routes[i].isOptimal ? sf::Color(80, 255, 160)
                : sf::Color(230, 230, 230));
            routeInfo.setPosition(mousePos.x + 30, mousePos.y + yOffset);
            window.draw(routeInfo);

            yOffset += lineHeight;
            displayCount++;
        }
    }

    if (outgoingCount > maxVisibleRoutes) {
        sf::Text more("", font, 11);
        more.setString("... and " + intToString(outgoingCount - maxVisibleRoutes) + " more");
        more.setFillColor(sf::Color(150, 150, 150));
        more.setStyle(sf::Text::Italic);
        more.setPosition(mousePos.x + 30, mousePos.y + yOffset);
        window.draw(more);
    }
}

void clampViewToMap(sf::View& view, const sf::Vector2u& mapSize) {
    sf::FloatRect bounds(0.f, 0.f,
        static_cast<float>(mapSize.x),
        static_cast<float>(mapSize.y));
    sf::Vector2f c = view.getCenter();
    sf::Vector2f s = view.getSize();
    float hw = s.x / 2.f;
    float hh = s.y / 2.f;

    if (c.x - hw < bounds.left)                 c.x = bounds.left + hw;
    if (c.y - hh < bounds.top)                  c.y = bounds.top + hh;
    if (c.x + hw > bounds.left + bounds.width)  c.x = bounds.left + bounds.width - hw;
    if (c.y + hh > bounds.top + bounds.height)  c.y = bounds.top + bounds.height - hh;
    view.setCenter(c);
}

// Lat/lon inverse for HUD cursor display
sf::Vector2f pixelToLatLon(const sf::Vector2f& pixel, const sf::Vector2u& imgSize, float invScale) {
    float x = pixel.x * invScale;
    float y = pixel.y * invScale;
    double lon = (x / imgSize.x) * 360.0 - 180.0;
    double lat = 90.0 - (y / imgSize.y) * 180.0;
    return sf::Vector2f(static_cast<float>(lat), static_cast<float>(lon));
}

// Create animated ship markers along routes (subset) for maritime feel
void initShips() {
    shipCount = 0;
    for (int i = 0; i < routeCount && shipCount < 64; ++i) {
        if (!routes[i].valid) continue;
        if (i % 35 == 0) {
            ships[shipCount].routeIndex = i;
            ships[shipCount].t = static_cast<float>((i % 100) / 100.0);
            ships[shipCount].speed = 0.03f + (i % 5) * 0.01f;
            shipCount++;
        }
    }
}

sf::Vector2f interpolateOnPath(const sf::Vector2f* points, int count, float t) {
    if (count < 2) return points[0];
    if (t <= 0.f) return points[0];
    if (t >= 1.f) return points[count - 1];

    // Simple piecewise linear interpolation along segments
    float totalLen = 0.f;
    float segLen[32];
    for (int i = 0; i < count - 1; ++i) {
        float dx = points[i + 1].x - points[i].x;
        float dy = points[i + 1].y - points[i].y;
        segLen[i] = std::sqrt(dx * dx + dy * dy);
        totalLen += segLen[i];
    }
    if (totalLen == 0.f) return points[0];

    float target = t * totalLen;
    float acc = 0.f;
    for (int i = 0; i < count - 1; ++i) {
        if (target <= acc + segLen[i]) {
            float localT = (target - acc) / segLen[i];
            return points[i] + (points[i + 1] - points[i]) * localT;
        }
        acc += segLen[i];
    }
    return points[count - 1];
}

// Draw ships moving on routes
void drawShips(sf::RenderWindow& window, float dt) {
    for (int s = 0; s < shipCount; ++s) {
        int rIndex = ships[s].routeIndex;
        if (rIndex < 0 || rIndex >= routeCount || !routes[rIndex].valid) continue;

        ships[s].t += ships[s].speed * dt;
        if (ships[s].t > 1.f) ships[s].t -= 1.f;

        sf::Vector2f a = ports[routes[rIndex].originIndex].pixel;
        sf::Vector2f b = ports[routes[rIndex].destinationIndex].pixel;
        sf::Vector2f pathPoints[MAX_WAYPOINTS];
        int pathLength = 0;
        generateOceanPath(a, b, routes[rIndex].originIndex, routes[rIndex].destinationIndex,
            pathPoints, pathLength);

        sf::Vector2f pos = interpolateOnPath(pathPoints, pathLength, ships[s].t);

        sf::CircleShape shipShape(3.f);
        shipShape.setOrigin(3.f, 3.f);
        shipShape.setFillColor(sf::Color(200, 255, 255));
        shipShape.setOutlineThickness(1.f);
        shipShape.setOutlineColor(sf::Color(0, 80, 120));
        shipShape.setPosition(pos);
        window.draw(shipShape);
    }
}

// Draw background gradient and vignette for dramatic look
void drawBackground(sf::RenderWindow& window, unsigned int width, unsigned int height, float timeSec) {
    sf::VertexArray grad(sf::Quads, 4);
    sf::Color top(4, 10, 25);
    sf::Color mid(3, 29, 54);
    sf::Color bot(2, 18, 36);

    grad[0].position = sf::Vector2f(0.f, 0.f);
    grad[1].position = sf::Vector2f(static_cast<float>(width), 0.f);
    grad[2].position = sf::Vector2f(static_cast<float>(width), static_cast<float>(height));
    grad[3].position = sf::Vector2f(0.f, static_cast<float>(height));

    grad[0].color = top;
    grad[1].color = top;
    grad[2].color = bot;
    grad[3].color = bot;

    window.draw(grad);

    // Subtle animated ocean glow arc behind the map
    float centerY = height * 0.55f;
    sf::CircleShape glow(static_cast<float>(width));
    glow.setOrigin(static_cast<float>(width), static_cast<float>(width));
    glow.setPosition(width / 2.f, centerY);
    sf::Uint8 alpha = static_cast<sf::Uint8>(30 + 15 * std::sin(timeSec * 0.8f));
    glow.setFillColor(sf::Color(0, 80, 160, alpha));
    window.draw(glow);

    // Vignette
    sf::RectangleShape vignette(sf::Vector2f(static_cast<float>(width), static_cast<float>(height)));
    vignette.setFillColor(sf::Color(0, 0, 0, 120));
    vignette.setOutlineThickness(0.f);
    vignette.setPosition(0.f, 0.f);
    window.draw(vignette);
}

// Draw lat/lon grid over the map
void drawLatLonGrid(sf::RenderWindow& window,
    const sf::Vector2u& mapSize,
    float scale,
    sf::Font& font,
    const sf::View& view)
{
    window.setView(view);
    const float latStep = 30.f;
    const float lonStep = 60.f;

    // vertical lines (longitude)
    for (float lon = -180.f; lon <= 180.1f; lon += lonStep) {
        sf::Vector2f p1 = latlonToPixel(90.f, lon, mapSize);
        sf::Vector2f p2 = latlonToPixel(-90.f, lon, mapSize);
        p1 *= scale;
        p2 *= scale;
        sf::Vertex line[2];
        line[0].position = p1;
        line[1].position = p2;
        line[0].color = sf::Color(40, 80, 110, 70);
        line[1].color = sf::Color(40, 80, 110, 70);
        window.draw(line, 2, sf::Lines);

        sf::Text label(intToString(static_cast<int>(lon)) + "°", font, 9);
        label.setFillColor(sf::Color(120, 170, 210, 120));
        label.setPosition(p1.x + 2.f, p1.y + 2.f);
        window.draw(label);
    }

    // horizontal lines (latitude)
    for (float lat = -60.f; lat <= 90.1f; lat += latStep) {
        sf::Vector2f p1 = latlonToPixel(lat, -180.f, mapSize);
        sf::Vector2f p2 = latlonToPixel(lat, 180.f, mapSize);
        p1 *= scale;
        p2 *= scale;
        sf::Vertex line[2];
        line[0].position = p1;
        line[1].position = p2;
        line[0].color = sf::Color(40, 80, 110, 70);
        line[1].color = sf::Color(40, 80, 110, 70);
        window.draw(line, 2, sf::Lines);

        sf::Text label(intToString(static_cast<int>(lat)) + "°", font, 9);
        label.setFillColor(sf::Color(120, 170, 210, 120));
        label.setPosition(p1.x + 2.f, p1.y + 2.f);
        window.draw(label);
    }
}

// Draw simple scale bar
void drawScaleBar(sf::RenderWindow& window,
    const sf::View& view,
    sf::Font& font,
    const sf::Vector2u& mapSize,
    float scale)
{
    window.setView(window.getDefaultView());
    float barWidthPx = 120.f;
    float barHeight = 6.f;

    sf::RectangleShape bar(sf::Vector2f(barWidthPx, barHeight));
    bar.setPosition(40.f, window.getSize().y - 70.f);
    bar.setFillColor(sf::Color(230, 230, 230));
    window.draw(bar);

    sf::RectangleShape barShadow(sf::Vector2f(barWidthPx, barHeight));
    barShadow.setPosition(40.f, window.getSize().y - 68.f);
    barShadow.setFillColor(sf::Color(0, 0, 0, 80));
    window.draw(barShadow);

    sf::Text label("", font, 11);
    label.setFillColor(sf::Color(220, 220, 220));
    label.setPosition(40.f, window.getSize().y - 88.f);
    label.setString("Approx. scale bar (visual only)");
    window.draw(label);
}

// Simple compass rose / north arrow
void drawCompass(sf::RenderWindow& window, sf::Font& font) {
    window.setView(window.getDefaultView());
    float cx = window.getSize().x - 70.f;
    float cy = 70.f;

    sf::CircleShape outer(30.f);
    outer.setOrigin(30.f, 30.f);
    outer.setPosition(cx, cy);
    outer.setFillColor(sf::Color(0, 0, 0, 120));
    outer.setOutlineThickness(1.5f);
    outer.setOutlineColor(sf::Color(180, 220, 255, 200));
    window.draw(outer);

    sf::ConvexShape arrow;
    arrow.setPointCount(3);
    arrow.setPoint(0, sf::Vector2f(cx, cy - 24.f));
    arrow.setPoint(1, sf::Vector2f(cx - 8.f, cy + 8.f));
    arrow.setPoint(2, sf::Vector2f(cx + 8.f, cy + 8.f));
    arrow.setFillColor(sf::Color(200, 0, 0));
    window.draw(arrow);

    sf::Text nText("N", font, 13);
    nText.setFillColor(sf::Color(255, 255, 255));
    nText.setPosition(cx - 5.f, cy - 22.f);
    window.draw(nText);
}

// ----------------- Main -----------------
int main() {
    loadPorts();
    loadOceanWaypoints();

    const string routeFile = "routes.txt";
    const string mapFile = "world_map.png";

    if (!loadRoutes(routeFile)) return 1;

    sf::Texture mapTexture;
    if (!mapTexture.loadFromFile(mapFile)) {
        cerr << "Failed to load map: " << mapFile << endl;
        return 1;
    }
    mapTexture.setSmooth(true);

    sf::Sprite mapSprite(mapTexture);
    sf::Vector2u mapSize = mapTexture.getSize();

    for (int i = 0; i < portCount; ++i)
        ports[i].pixel = latlonToPixel(ports[i].lat, ports[i].lon, mapSize);

    for (int i = 0; i < WAYPOINT_COUNT; ++i)
        oceanWaypoints[i].pixel = latlonToPixel(oceanWaypoints[i].lat, oceanWaypoints[i].lon, mapSize);

    sf::VideoMode desktop = getDesktopMode();
    unsigned int windowWidth = static_cast<unsigned int>(desktop.width * 0.9f);
    unsigned int windowHeight = static_cast<unsigned int>(desktop.height * 0.9f);

    sf::ContextSettings settings;
    settings.antialiasingLevel = 8;

    sf::RenderWindow window(sf::VideoMode(windowWidth, windowHeight),
        "Maritime Navigation System - Global Shipping Routes",
        sf::Style::Default,
        settings);
    window.setFramerateLimit(60);

    float scaleX = static_cast<float>(windowWidth) / mapSize.x;
    float scaleY = static_cast<float>(windowHeight) / mapSize.y;
    float scale = (scaleX < scaleY) ? scaleX : scaleY;
    mapSprite.setScale(scale, scale);

    for (int i = 0; i < portCount; ++i) {
        ports[i].pixel.x *= scale;
        ports[i].pixel.y *= scale;
    }
    for (int i = 0; i < WAYPOINT_COUNT; ++i) {
        oceanWaypoints[i].pixel.x *= scale;
        oceanWaypoints[i].pixel.y *= scale;
    }

    sf::View view = window.getDefaultView();
    float zoom = 1.f;

    sf::Font font;
    if (!font.loadFromFile("Arial.ttf")) {
        font.loadFromFile("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    }

    sf::CircleShape portShape(PORT_RADIUS);
    portShape.setOrigin(PORT_RADIUS, PORT_RADIUS);
    portShape.setFillColor(sf::Color::White);
    portShape.setOutlineThickness(1.f);
    portShape.setOutlineColor(sf::Color(120, 120, 120));

    sf::CircleShape portGlow(PORT_RADIUS * 2.2f);
    portGlow.setOrigin(PORT_RADIUS * 2.2f, PORT_RADIUS * 2.2f);
    portGlow.setFillColor(sf::Color(255, 255, 255, 40));

    sf::CircleShape preferredHalo(PORT_RADIUS * 3.6f);
    preferredHalo.setOrigin(PORT_RADIUS * 3.6f, PORT_RADIUS * 3.6f);
    preferredHalo.setFillColor(sf::Color(255, 215, 0, 55));

    sf::CircleShape evaluatedHalo(PORT_RADIUS * 2.8f);
    evaluatedHalo.setOrigin(PORT_RADIUS * 2.8f, PORT_RADIUS * 2.8f);
    evaluatedHalo.setFillColor(sf::Color(0, 255, 255, 50));

    sf::CircleShape hubHalo(PORT_RADIUS * 4.2f);
    hubHalo.setOrigin(PORT_RADIUS * 4.2f, PORT_RADIUS * 4.2f);
    hubHalo.setFillColor(sf::Color(160, 220, 255, 45));

    sf::CircleShape portHighlight(HIGHLIGHT_RADIUS);
    portHighlight.setOrigin(HIGHLIGHT_RADIUS, HIGHLIGHT_RADIUS);
    portHighlight.setOutlineThickness(2.f);
    portHighlight.setOutlineColor(sf::Color::Yellow);
    portHighlight.setFillColor(sf::Color::Transparent);

    bool dragging = false;
    sf::Vector2i lastMouse;
    int hoveredPort = -1, hoveredRoute = -1, selectedRoute = -1;

    string queryText = "";
    bool queryActive = false;
    bool journeyMode = false;

    initShips();

    sf::Clock clock;
    while (window.isOpen()) {
        float dt = clock.restart().asSeconds();
        static float totalTime = 0.f;
        totalTime += dt;

        sf::Event e;
        while (window.pollEvent(e)) {
            if (e.type == sf::Event::Closed) window.close();

            if (e.type == sf::Event::MouseWheelScrolled) {
                sf::Vector2i pixelPos(e.mouseWheelScroll.x, e.mouseWheelScroll.y);
                sf::Vector2f before = window.mapPixelToCoords(pixelPos, view);

                zoom *= (e.mouseWheelScroll.delta > 0) ? 0.9f : 1.1f;
                if (zoom < 0.1f) zoom = 0.1f;
                if (zoom > 10.f) zoom = 10.f;

                view.setSize(window.getDefaultView().getSize().x * zoom,
                    window.getDefaultView().getSize().y * zoom);
                sf::Vector2f after = window.mapPixelToCoords(pixelPos, view);
                view.move(before - after);
                clampViewToMap(view, mapSize);
            }

            if (e.type == sf::Event::MouseButtonPressed &&
                e.mouseButton.button == sf::Mouse::Left) {
                dragging = true;
                lastMouse = sf::Mouse::getPosition(window);
            }

            if (e.type == sf::Event::MouseButtonReleased &&
                e.mouseButton.button == sf::Mouse::Left) {
                dragging = false;

                if (hoveredRoute != -1 && !journeyMode) {
                    selectedRoute = (selectedRoute == hoveredRoute) ? -1 : hoveredRoute;
                }

                if (journeyMode && hoveredPort != -1) {
                    // Simple demo: choose cheapest outgoing route from this port
                    int bestIdx = -1;
                    int bestCost = 1e9;
                    for (int i = 0; i < routeCount; ++i) {
                        if (!routes[i].valid) continue;
                        if (routes[i].originIndex == hoveredPort && routes[i].vCost < bestCost) {
                            bestCost = routes[i].vCost;
                            bestIdx = i;
                        }
                    }
                    if (bestIdx != -1 && currentJourneyLen < 32) {
                        currentJourney[currentJourneyLen++].routeIndex = bestIdx;
                        routes[bestIdx].isOptimal = true; // highlight visually
                    }
                }
            }

            if (e.type == sf::Event::MouseButtonReleased &&
                e.mouseButton.button == sf::Mouse::Right) {
                if (hoveredPort != -1) {
                    ports[hoveredPort].isPreferred = !ports[hoveredPort].isPreferred;
                }
            }

            if (e.type == sf::Event::MouseMoved && dragging) {
                sf::Vector2i now = sf::Mouse::getPosition(window);
                sf::Vector2f delta = window.mapPixelToCoords(lastMouse, view) -
                    window.mapPixelToCoords(now, view);
                view.move(delta);
                lastMouse = now;
                clampViewToMap(view, mapSize);
            }

            if (e.type == sf::Event::TextEntered && queryActive) {
                if (e.text.unicode == 8) { // backspace
                    if (!queryText.empty())
                        queryText.erase(queryText.size() - 1, 1);
                }
                else if (e.text.unicode == 13) { // enter
                    queryActive = false;
                }
                else if (e.text.unicode >= 32 && e.text.unicode < 128) {
                    queryText += static_cast<char>(e.text.unicode);
                }
            }

            if (e.type == sf::Event::KeyPressed) {
                if (e.key.code == sf::Keyboard::R) {
                    view = window.getDefaultView();
                    zoom = 1.f;
                }
                if (e.key.code == sf::Keyboard::Escape) {
                    window.close();
                }
                if (e.key.code == sf::Keyboard::Slash) {
                    queryActive = true;
                    queryText.clear();
                }
                if (e.key.code == sf::Keyboard::J) {
                    journeyMode = !journeyMode;
                    if (!journeyMode) {
                        for (int i = 0; i < routeCount; ++i)
                            routes[i].isOptimal = false;
                        currentJourneyLen = 0;
                    }
                }

                // Simulated algorithm visual toggles
                if (e.key.code == sf::Keyboard::F1) {
                    for (int i = 0; i < portCount; ++i)
                        ports[i].isEvaluated = (i % 3 == 0);
                    for (int i = 0; i < routeCount; ++i)
                        routes[i].isEvaluated = (i % 4 == 0);
                }
                if (e.key.code == sf::Keyboard::F2) {
                    for (int i = 0; i < routeCount; ++i)
                        routes[i].isLayoverEdge = (i % 5 == 0);
                }
                if (e.key.code == sf::Keyboard::F3) {
                    for (int i = 0; i < portCount; ++i) {
                        ports[i].isEvaluated = false;
                    }
                    for (int i = 0; i < routeCount; ++i) {
                        routes[i].isEvaluated = false;
                        routes[i].isLayoverEdge = false;
                        routes[i].isOptimal = false;
                    }
                    currentJourneyLen = 0;
                }
            }
        }

        // Hover detection
        sf::Vector2i mousePix = sf::Mouse::getPosition(window);
        sf::Vector2f world = window.mapPixelToCoords(mousePix, view);
        hoveredPort = -1;
        hoveredRoute = -1;

        for (int i = 0; i < portCount; ++i) {
            float dx = ports[i].pixel.x - world.x;
            float dy = ports[i].pixel.y - world.y;
            if (std::sqrt(dx * dx + dy * dy) <= 10.f * zoom) {
                hoveredPort = i;
                break;
            }
        }

        for (int i = 0; i < routeCount; ++i) {
            if (!routes[i].valid) continue;
            sf::Vector2f a = ports[routes[i].originIndex].pixel;
            sf::Vector2f b = ports[routes[i].destinationIndex].pixel;
            sf::Vector2f ap = world - a;
            sf::Vector2f ab = b - a;
            float ab2 = ab.x * ab.x + ab.y * ab.y;
            if (ab2 == 0) continue;
            float t = (ap.x * ab.x + ap.y * ab.y) / ab2;
            if (t < 0) t = 0;
            if (t > 1) t = 1;
            sf::Vector2f proj = a + ab * t;
            float dist = std::sqrt((proj.x - world.x) * (proj.x - world.x) +
                (proj.y - world.y) * (proj.y - world.y));
            if (dist <= 8.f * zoom) {
                hoveredRoute = i;
                break;
            }
        }

        // Background and world view
        window.setView(window.getDefaultView());
        drawBackground(window, windowWidth, windowHeight, totalTime);

        window.setView(view);
        window.draw(mapSprite);

        // Optional grid overlays and compass
        drawLatLonGrid(window, mapSize, scale, font, view);

        // Graphical query / subgraph filter
        bool useFilter = !queryText.empty();
        auto matchesQuery = [&](int routeIdx) -> bool {
            if (!useFilter) return true;
            const Route& r = routes[routeIdx];
            string qp = queryText;
            for (size_t i = 0; i < qp.size(); ++i) qp[i] = std::tolower(qp[i]);

            string s1 = ports[r.originIndex].name;
            string s2 = ports[r.destinationIndex].name;
            string comp = r.company;
            for (size_t i = 0; i < s1.size(); ++i) s1[i] = std::tolower(s1[i]);
            for (size_t i = 0; i < s2.size(); ++i) s2[i] = std::tolower(s2[i]);
            for (size_t i = 0; i < comp.size(); ++i) comp[i] = std::tolower(comp[i]);

            return s1.find(qp) != string::npos ||
                s2.find(qp) != string::npos ||
                comp.find(qp) != string::npos;
            };

        // Routes
        for (int i = 0; i < routeCount; ++i) {
            if (!routes[i].valid) continue;

            bool visible = matchesQuery(i);
            sf::Vector2f a = ports[routes[i].originIndex].pixel;
            sf::Vector2f b = ports[routes[i].destinationIndex].pixel;

            bool isSel = (i == selectedRoute);
            bool isHover = (i == hoveredRoute);
            bool fromHover = (hoveredPort == routes[i].originIndex ||
                hoveredPort == routes[i].destinationIndex);
            bool isPreferredEdge = ports[routes[i].originIndex].isPreferred ||
                ports[routes[i].destinationIndex].isPreferred;

            sf::Color lineColor;
            float thicknessFactor = 1.f;

            if (!visible) {
                lineColor = sf::Color(60, 65, 80, 40);
                thicknessFactor = 0.6f;
            }
            else if (routes[i].isOptimal) {
                lineColor = sf::Color(0, 255, 150, 240);
                thicknessFactor = 2.7f;
            }
            else if (isSel) {
                lineColor = sf::Color(0, 255, 255, 230);
                thicknessFactor = 2.2f;
            }
            else if (isHover) {
                lineColor = sf::Color(255, 255, 0, 220);
                thicknessFactor = 2.0f;
            }
            else if (isPreferredEdge) {
                lineColor = sf::Color(255, 180, 0, 210);
                thicknessFactor = 1.8f;
            }
            else if (routes[i].isEvaluated) {
                lineColor = sf::Color(150, 210, 255, 190);
                thicknessFactor = 1.4f;
            }
            else {
                lineColor = sf::Color(120, 150, 180, 90);
                thicknessFactor = 1.0f;
            }

            sf::Vector2f pathPoints[MAX_WAYPOINTS];
            int pathLength = 0;
            generateOceanPath(a, b, routes[i].originIndex, routes[i].destinationIndex,
                pathPoints, pathLength);

            bool dashed = routes[i].isLayoverEdge;
            drawOceanRoute(window, pathPoints, pathLength, lineColor, thicknessFactor, dashed);

            if (pathLength >= 2) {
                sf::Vector2f tail = pathPoints[pathLength - 2];
                sf::Vector2f head = pathPoints[pathLength - 1];
                drawArrowHead(window, tail, head, lineColor);
            }
        }

        // Danger zones / maritime overlays (simple rectangles)
        {
            sf::RectangleShape piracy(sf::Vector2f(200.f, 140.f));
            piracy.setFillColor(sf::Color(200, 80, 0, 40));
            piracy.setOutlineThickness(1.5f);
            piracy.setOutlineColor(sf::Color(255, 120, 0, 150));
            // Approximate Gulf of Aden / Somali basin region
            sf::Vector2f p1 = latlonToPixel(18.0, 55.0, mapSize) * scale;
            piracy.setPosition(p1.x - 100.f, p1.y);
            window.draw(piracy);

            sf::Text warn("High-risk area", font, 10);
            warn.setFillColor(sf::Color(255, 200, 150, 200));
            warn.setPosition(piracy.getPosition().x + 8.f, piracy.getPosition().y + 6.f);
            window.draw(warn);
        }

        // Ports (nodes)
        for (int i = 0; i < portCount; ++i) {
            sf::Vector2f pos = ports[i].pixel;
            float glowScale = (zoom < 1.f) ? 1.f : (1.f / zoom);

            portGlow.setScale(glowScale, glowScale);
            portGlow.setPosition(pos);
            window.draw(portGlow);

            if (ports[i].isHub) {
                hubHalo.setScale(glowScale * 1.2f, glowScale * 1.2f);
                hubHalo.setPosition(pos);
                window.draw(hubHalo);
            }
            if (ports[i].isPreferred) {
                preferredHalo.setScale(glowScale * 1.1f, glowScale * 1.1f);
                preferredHalo.setPosition(pos);
                window.draw(preferredHalo);
            }
            if (ports[i].isEvaluated) {
                evaluatedHalo.setScale(glowScale * 0.9f, glowScale * 0.9f);
                evaluatedHalo.setPosition(pos);
                window.draw(evaluatedHalo);
            }

            portShape.setPosition(pos);
            window.draw(portShape);

            if (i == hoveredPort) {
                portHighlight.setPosition(pos);
                window.draw(portHighlight);
            }

            sf::Text label(ports[i].name, font, ports[i].isHub ? 11 : 9);
            label.setFillColor(sf::Color(240, 240, 240));
            label.setPosition(pos.x + 6.f, pos.y - 4.f);
            window.draw(label);
        }

        // Animated ships
        drawShips(window, dt);

        // Tooltips
        window.setView(window.getDefaultView());
        if (hoveredPort != -1) {
            drawPortDropdown(window, font, hoveredPort, mousePix);
        }
        else if (hoveredRoute != -1) {
            Route& r = routes[hoveredRoute];

            sf::RectangleShape shadow(sf::Vector2f(404.f, 76.f));
            shadow.setPosition(mousePix.x + 13, mousePix.y + 13);
            shadow.setFillColor(sf::Color(0, 0, 0, 90));
            window.draw(shadow);

            sf::RectangleShape tooltip(sf::Vector2f(400.f, 72.f));
            tooltip.setPosition(mousePix.x + 15, mousePix.y + 15);
            tooltip.setFillColor(sf::Color(24, 24, 32, 240));
            tooltip.setOutlineThickness(2.f);
            tooltip.setOutlineColor(sf::Color::Yellow);
            window.draw(tooltip);

            sf::Text info1("", font, 13);
            info1.setString(ports[r.originIndex].name + " -> " +
                ports[r.destinationIndex].name +
                "  [" + r.company + "]");
            info1.setFillColor(sf::Color::White);
            info1.setStyle(sf::Text::Bold);
            info1.setPosition(mousePix.x + 25, mousePix.y + 20);
            window.draw(info1);

            sf::Text info2("", font, 11);
            info2.setString(r.date + " | " + r.depTime + " - " + r.arrTime +
                "  |  Cost: $" + intToString(r.vCost));
            info2.setFillColor(sf::Color(210, 210, 210));
            info2.setPosition(mousePix.x + 25, mousePix.y + 40);
            window.draw(info2);

            sf::Text info3("", font, 11);
            string flags = "";
            if (r.isOptimal)      flags += "Optimal route   ";
            if (r.isLayoverEdge)  flags += "Layover leg   ";
            if (r.isEvaluated)    flags += "Evaluated edge";
            if (flags.empty())    flags = "Standard route";
            info3.setString(flags);
            info3.setFillColor(sf::Color(150, 220, 255));
            info3.setPosition(mousePix.x + 25, mousePix.y + 58);
            window.draw(info3);
        }

        // Right-side panel: journeys, layovers, stats
        float sideW = 270.f;
        sf::RectangleShape sidePanel(sf::Vector2f(sideW, static_cast<float>(windowHeight)));
        sidePanel.setPosition(static_cast<float>(windowWidth) - sideW, 0.f);
        sidePanel.setFillColor(sf::Color(3, 8, 16, 230));
        sidePanel.setOutlineThickness(1.5f);
        sidePanel.setOutlineColor(sf::Color(40, 80, 120));
        window.draw(sidePanel);

        float px = static_cast<float>(windowWidth) - sideW + 12.f;
        float py = 16.f;

        sf::Text title("Maritime Route Console", font, 14);
        title.setFillColor(sf::Color(180, 230, 255));
        title.setStyle(sf::Text::Bold);
        title.setPosition(px, py);
        window.draw(title);
        py += 24.f;

        sf::Text jm("", font, 11);
        jm.setFillColor(sf::Color(210, 210, 210));
        jm.setString(string("Journey mode [J]: ") + (journeyMode ? "ON" : "OFF"));
        jm.setPosition(px, py);
        window.draw(jm);
        py += 18.f;

        // Journey details
        if (currentJourneyLen == 0) {
            sf::Text hint("", font, 11);
            hint.setFillColor(sf::Color(160, 160, 160));
            hint.setString("Click ports in Journey mode\n"
                "to auto-build a multi-leg path\n"
                "(cheapest outgoing leg each time).\n"
                "Ideal to demo advanced route\n"
                "generation & layovers.");
            hint.setPosition(px, py);
            window.draw(hint);
            py += 70.f;
        }
        else {
            int totalCost = 0;
            int minCost = 1000000000;
            int maxCost = 0;
            for (int i = 0; i < currentJourneyLen; ++i) {
                int idx = currentJourney[i].routeIndex;
                totalCost += routes[idx].vCost;
                if (routes[idx].vCost < minCost) minCost = routes[idx].vCost;
                if (routes[idx].vCost > maxCost) maxCost = routes[idx].vCost;
            }

            sf::Text summary("", font, 11);
            summary.setFillColor(sf::Color(220, 220, 220));
            summary.setString("Legs: " + intToString(currentJourneyLen) +
                "\nTotal cost: $" + intToString(totalCost) +
                "\nMin leg: $" + intToString(minCost) +
                "  Max leg: $" + intToString(maxCost));
            summary.setPosition(px, py);
            window.draw(summary);
            py += 52.f;

            for (int i = 0; i < currentJourneyLen; ++i) {
                const Route& r = routes[currentJourney[i].routeIndex];
                sf::Text leg("", font, 11);
                string legText = intToString(i + 1) + ". " +
                    ports[r.originIndex].name + " -> " +
                    ports[r.destinationIndex].name +
                    "  $" + intToString(r.vCost);
                leg.setString(legText);
                leg.setFillColor(sf::Color(200, 230, 255));
                leg.setPosition(px, py);
                window.draw(leg);
                py += 16.f;
            }
        }

        py += 8.f;
        sf::Text layHdr("Layover & Queue View (F2)", font, 12);
        layHdr.setFillColor(sf::Color(180, 220, 255));
        layHdr.setStyle(sf::Text::Bold);
        layHdr.setPosition(px, py);
        window.draw(layHdr);
        py += 18.f;

        sf::Text layTxt("", font, 11);
        layTxt.setFillColor(sf::Color(200, 200, 200));
        layTxt.setString("Dashed legs represent layover segments.\n"
            "Attach them to a queue or priority\n"
            "queue in your data structures layer\n"
            "to calculate realistic transit times.");
        layTxt.setPosition(px, py);
        window.draw(layTxt);
        py += 60.f;

        sf::Text evalHdr("Algorithm Visualization (F1/F3)", font, 12);
        evalHdr.setFillColor(sf::Color(180, 220, 255));
        evalHdr.setStyle(sf::Text::Bold);
        evalHdr.setPosition(px, py);
        window.draw(evalHdr);
        py += 18.f;

        sf::Text evalTxt("", font, 11);
        evalTxt.setFillColor(sf::Color(200, 200, 200));
        evalTxt.setString("Cyan halos = evaluated nodes\n"
            "Bright blue edges = evaluated routes\n"
            "Green edges = current optimal path.");
        evalTxt.setPosition(px, py);
        window.draw(evalTxt);

        // Bottom instruction bar + cursor info + zoom
        sf::RectangleShape instrBg(sf::Vector2f(static_cast<float>(windowWidth), 32.f));
        instrBg.setPosition(0.f, static_cast<float>(windowHeight) - 32.f);
        instrBg.setFillColor(sf::Color(0, 0, 0, 200));
        window.draw(instrBg);

        sf::Text instructions("", font, 11);
        instructions.setFillColor(sf::Color(230, 230, 230));
        instructions.setPosition(10.f, static_cast<float>(windowHeight) - 26.f);
        instructions.setString(
            "Zoom: Wheel | Pan: Drag | R: Reset | ESC: Exit | /: Query | "
            "F1/F3: Show/Clear algorithm states | F2: Layover edges | "
            "J: Journey mode | Right-click port: Preferred"
        );
        window.draw(instructions);

        // Query bar
        float qHeight = 22.f;
        sf::RectangleShape qBg(sf::Vector2f(static_cast<float>(windowWidth) * 0.6f, qHeight));
        qBg.setPosition(8.f, 8.f);
        qBg.setFillColor(sf::Color(0, 0, 0, 160));
        qBg.setOutlineThickness(1.f);
        qBg.setOutlineColor(queryActive ? sf::Color(0, 200, 255) : sf::Color(80, 80, 80));
        window.draw(qBg);

        sf::Text qText("", font, 11);
        qText.setFillColor(sf::Color(220, 220, 220));
        string prefix = "Query [/]: ";
        string display = prefix + queryText + (queryActive ? "_" : "");
        qText.setString(display);
        qText.setPosition(12.f, 10.f);
        window.draw(qText);

        // Compass & scale bar
        drawCompass(window, font);
        drawScaleBar(window, view, font, mapSize, scale);

        // Cursor lat/lon + zoom in status bar
        sf::Vector2f worldAtCursor = window.mapPixelToCoords(mousePix, view);
        sf::Vector2f ll = pixelToLatLon(sf::Vector2f(worldAtCursor.x, worldAtCursor.y),
            mapSize, 1.f / scale);

        sf::Text status("", font, 11);
        status.setFillColor(sf::Color(190, 220, 255));
        status.setPosition(windowWidth * 0.62f, windowHeight - 26.f);
        string s = "Cursor: ";
        s += "Lat " + intToString(static_cast<int>(ll.x)) + "°, ";
        s += "Lon " + intToString(static_cast<int>(ll.y)) + "°   ";
        s += "Zoom x" + intToString(static_cast<int>(1.f / zoom));
        status.setString(s);
        window.draw(status);

        window.display();
    }

    return 0;
}