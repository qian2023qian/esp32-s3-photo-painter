#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""AI-Photo-Picker 统一入口：analyze / render / serve 子命令。"""

import argparse
from datetime import date


def cmd_analyze(args):
    import analyze_photos

    analyze_photos.require_exiftool()
    argv = []
    if args.cache:
        argv.append("--cache")
    if args.debug:
        argv.append("--debug")
    if args.concurrency and args.concurrency > 1:
        argv += ["-j", str(args.concurrency)]
    analyze_photos.main(argv)


def cmd_render(args):
    import config as cfg

    # CLI 覆盖 -> 写 cfg 模块属性，render_daily_photo 用 getattr(cfg,...) 读取
    if args.today:
        try:
            y, m, d = map(int, args.today.split("-"))
            cfg.RENDER_TODAY = date(y, m, d)
        except Exception:
            print(f"[WARN] --today 格式应为 YYYY-MM-DD，忽略 {args.today}")
    if args.count:
        cfg.DAILY_PHOTO_QUANTITY = args.count
    if args.pipeline:
        cfg.RENDER_PIPELINE = args.pipeline
    if args.dither:
        cfg.RENDER_DITHER = args.dither
    if args.no_push:
        cfg.PUSH_ENABLED = False
    if args.output:
        cfg.OUTPUT_DIR = args.output

    import render_daily_photo

    render_daily_photo.main()


def cmd_serve(args):
    import server

    server.main(host=args.host, port=args.port)


def main():
    p = argparse.ArgumentParser(prog="picker", description="AI-Photo-Picker：AI 选片 → 6 色渲染 → 推送相框")
    sub = p.add_subparsers(dest="cmd", required=True)

    pa = sub.add_parser("analyze", help="扫描相册并用 VLM 评分写入 photos.db")
    pa.add_argument("--cache", action="store_true", help="缓存文件列表（调试）")
    pa.add_argument("-j", "--concurrency", type=int, default=1, help="并发线程数")
    pa.add_argument("--debug", action="store_true", help="调试模式")
    pa.set_defaults(func=cmd_analyze)

    pr = sub.add_parser("render", help="选'历史上的今天'渲染 6 色 BMP 并推送相框")
    pr.add_argument("--today", help="指定日期 YYYY-MM-DD（测试用）")
    pr.add_argument("--count", type=int, help="选片数量")
    pr.add_argument("--pipeline", choices=["epd", "od"], help="渲染方案")
    pr.add_argument("--dither", help="抖动算法")
    pr.add_argument("--no-push", action="store_true", help="只出图不推送")
    pr.add_argument("--output", help="输出目录")
    pr.set_defaults(func=cmd_render)

    ps = sub.add_parser("serve", help="启动 Flask WebUI（review + 渲染推送）")
    ps.add_argument("--host", help="监听地址")
    ps.add_argument("--port", type=int, help="端口")
    ps.set_defaults(func=cmd_serve)

    args = p.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
