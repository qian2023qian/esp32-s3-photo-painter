# 照片库路径（你自己的相册目录）
IMAGE_DIR = "./test"

# 数据库路径（建议保持默认）
DB_PATH = "./photos.db"

# VLM 渠道列表（按优先级从高到低排列）
# 当某个渠道返回 429 时，自动尝试下一个渠道
API_CHANNELS = [
    {
        "api_url":    "http://127.0.0.1:1234/v1/chat/completions",
        "api_key":    "",
        "model_name": "qwen3-vl-32b-instruct",
    },
    # 可以添加更多渠道，例如：
    # {
    #     "api_url":    "https://other-provider.com/v1/chat/completions",
    #     "api_key":    "sk-xxxxxxxx",
    #     "model_name": "qwen-vl-plus",
    # },
]

# 每次最多处理多少张的图片
BATCH_LIMIT = None

# 请求超时时间（秒）
TIMEOUT = 600

# 某个渠道失败后，临时降低其优先级的冷却时间（秒）
# 例如 A 失败、B 成功后，在冷却期内后续照片会优先从 B 开始请求
CHANNEL_FAILOVER_COOLDOWN_SEC = 300

# Flask 静态服务
FLASK_HOST = "0.0.0.0"
FLASK_PORT = 8765
# 是否开启照片库 WebUI（前期检验提示词选片效果时使用，跑通后建议关闭）
ENABLE_REVIEW_WEBUI = True

# 离线中文城市名索引，使用 geonames 数据制作
WORLD_CITIES_CSV = "./data/world_cities_zh.csv"

# 网格大小（纬度/经度度数）；越大越快但精度略差。1.0 对大多数场景够用。
CITY_GRID_DEG = 1.0

# 你的“常驻常驻”坐标（用于判断是否为旅行期间照片，从而对评分进行小幅加成）
# 照片 GPS 距离常驻地超过 HOME_RADIUS_KM，则视为“异地”
# 默认值给了深圳市中心附近（不改也能保持原行为的大致效果）
HOME_LAT = 22.543096
HOME_LON = 114.057865
HOME_RADIUS_KM = 60.0

# 最大接受距离（公里），超出则认为“不在任何城市附近”
CITY_MAX_DISTANCE_KM = 100.0

# 渲染 BMP 输出目录（render 子命令产物）
OUTPUT_DIR = "./output"

# 自定义字体路径（为空则退回默认字体）
FONT_PATH = ""

# 每日选片“精彩度”阈值
MEMORY_THRESHOLD = 70.0

# 每日挑选的照片数量
DAILY_PHOTO_QUANTITY = 5

# ===== ESP32 相框推送 =====
ESP32_HOST = "192.168.4.1"   # 相框 IP（AP 模式默认网关；STA 模式填 DHCP 地址）
ESP32_PORT = 80
PUSH_ENABLED = True          # 关掉则只落盘 BMP，不推送相框
AI_NAME_PREFIX = "ai_"       # 相框固件以此前缀识别来源文件（配合 300 张自动清理）
AI_MAX_FILES = 300           # 相框固件清理阈值（与固件常量对齐，仅供参考）

# ===== 渲染方案与默认调色（render 子命令 + WebUI 初始值） =====
RENDER_PIPELINE = "epd"      # "epd"(epdoptimize 风格) / "od"(OpenDisplay 风格)
RENDER_DITHER = "floydSteinberg"  # floydSteinberg/atkinson/jarvis/stucki/burkes/sierra3/sierra2
RENDER_ADJ = {"br": 0, "ct": 20, "st": 20, "sh": 50, "df": 100}

# ===== WebUI 静态资源回退源（serve 用，保证前端 bundle 与固件同源） =====
ESP32_FIRMWARE_BUNDLE_DIR = "E:/ESP32/ESP-PROJECTS/ESP32-S3-PhotoPainter/ESP32-S3-6Color-PhotoFrame/components/user_app_bsp/mode_src"
