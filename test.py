{
    "video_aspect_ratio": "16:9",
    "output_path": "output.mp4",
    "bg_color": "#000000",
    "tracks": [
        {
            # bắt buộc có
            "type": "video",
            "path": "input.mp4",
            "video_scale": 1.0,
            "flip_horizontal": False,
            "h" : "center", # nếu tỉ lệ video đầu vào khác với tỉ lệ video đầu ra thì có thể căn chỉnh vị trí video đầu vào theo chiều ngang hoặc chiều dọc
            "v" : "center",
            "resize_mode": "stretch", # fit, fill, stretch (fit: giữ nguyên tỉ lệ video đầu vào, fill: cắt bớt video đầu vào để vừa với video đầu ra, stretch: kéo dãn video đầu vào để vừa với video đầu ra)
        },
        {
            "type": "watermark",
            "text": "@hoanganhcoder",
            "font": "Noto Sans",
            "size": 5.0,
            "bold": True,
            "italic": True,
            "upper": True,
            "color": "#FFFFFF",
            "outline_color": "#000000",
            "bounce": True,
            "opacity": 0.28,
        },
        {
            "type": "logo",
            "path": "logo.png",
            "scale": 0.16,
            "opacity": 0.24,
            "position": {
                "x": 0.02,
                "y": 0.02,
            },
        },
        {
            "type": "gaussian_blur",
            "strength": 0.85,
            "feather": 36,
            "vertical_stretch": 1.0,
            "horizontal_blur": 0.4,
            "temporal_blend": 0.18,
            "regions": [
                {
                    "start": 0.0,
                    "end": 9999999.0,
                    "x": 0,
                    "y": 610,
                    "w": 1280,
                    "h": 90,
                }
            ],
        },
        {
            "type": "subtitle",
            "srt": "examples/sample.srt",
            "font": "Noto Sans",
            "size": 13.0,
            "bold": True,
            "italic": True,
            "upper": False,
            "color": "#FFF200",
            "outline_color": "#101010",
            "back_color": "#00000000",
            "outline": 4,
            "shadow": 0,
            "margin": 12,
            "regions": 
                {
                    "x": 0,
                    "y": 610,
                    "w": 1280,
                    "h": 90,
                }
            ,
        },
        {
            "type": "gaussian_blur",
            "strength": 0.85,
            "feather": 36,
            "vertical_stretch": 1.0,
            "horizontal_blur": 0.4,
            "temporal_blend": 0.18,
            "regions": [
                {
                    "start": 0.0,
                    "end": 9999999.0,
                    "x": 1000,
                    "y": 0,
                    "w": 280,
                    "h": 90,
                }
            ],
        },
        {
            "type" : "image", # thường là file png để làm teamplate
            "path" : "image.png",
            "w" : "100%",
            "h" : "100%",
            "resize_mode" : "stretch",
        }
        
    ],
    
}
