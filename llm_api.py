import argparse
import json
import re
from openai import OpenAI
import base64


client = OpenAI(
    base_url="https://ark.cn-beijing.volces.com/api/v3",
    api_key="6e9cdd8a-eb42-471f-a49c-9e53100a018e"
)

def analyze_animal(image_url):
    """
    调用大模型API进行动物识别和定位
    :param image_url: 需要分析的图片URL
    :return: 结构化分析结果
    """

    system_prompt = """
        Role: 图像动物识别专家
        Profile:
        language: 中文
        description: 专门识别图像中的动物类别和位置信息
        Goals:
        准确识别图像中的动物类别
        精确定位动物在图像中的位置
        输出标准化的识别结果
        Constrains:
        必须输出动物类别和位置框信息
        位置框格式必须为[起始x,起始y,宽,高]
        需要描述识别依据和分析过程
        Skills:
        精通计算机视觉和图像识别
        擅长物体检测和定位
        熟悉常见动物特征识别
        能够处理复杂背景下的动物识别
        Workflows:
        接收用户输入的图像
        分析图像内容，识别可能的动物区域
        确定动物类别和精确位置
        计算位置框坐标[起始x,起始y,宽,高]
        生成详细的分析报告
        按照标准格式输出结果
        OutputFormat:
        ""
        输入图像分析结果：

        识别依据：

        特征识别：
        位置分析：
        最终结果：
        {
        "类别": [动物类别(只区分猫、狗和其他动物),若有多个动物,用逗号隔开,必须使用方括号],
        "位置框": [起始x, 起始y, 宽, 高]
        }
        ""
        """
    
    response = client.chat.completions.create(
        model="ep-20250513173018-dm7j2",
        messages=[
            {"role": "system", "content": system_prompt},
            {
                "role": "user",
                "content": [
                    {
                        "type": "image_url",
                        "image_url": {
                            "url": image_url
                        }
                    },
                    {"type": "text", "text": "请分析这张图片中的动物"}
                ]
            }
        ],
        temperature=0.1 # 使用更低的随机性确保坐标准确
    )
    
    raw_result = response.choices[0].message.content
    result = parse_animal_result(raw_result)
    
    return {"type": result.group(1).strip() if result else None}

def parse_animal_result(raw_result):
    type_animal = re.search(r'\"类别\": \[(.*?)\]', raw_result)
    return type_animal

def analyze_emotion(text):
    system_prompt = """
        Role: 细粒度情绪识别专家
        Profile:
        language: 中文
        description: 专门识别文本中的细粒度情绪类型(积极、中性、消极)，并提供详细分析
        Goals:
        准确识别输入文本中的情绪类型
        提供可读性强的分析结果
        保持输出格式标准化
        Constrains:
        必须从几种情绪类型中选择最匹配的一种
        分析过程必须详细且有理有据
        输出格式必须严格遵循模板
        Skills:
        精通心理学和情感分析
        擅长文本细粒度分析
        熟练掌握情绪识别技巧
        能够识别隐晦的情绪表达(特别是"阴阳怪气"类)
        Workflows:
        接收用户输入的文本
        分析文本中的关键词、语气、修辞等特征
        评估各情绪类型的匹配程度
        选择最匹配的情绪类型
        生成详细的分析报告
        按照标准格式输出结果

        OutputFormat:
        ""
        分析过程：

        关键词分析：[必须使用方括号，这里列出关键词]
        语气分析：[必须使用方括号，这里列出语气分析内容]
        其他特征分析：[必须使用方括号，这里列出其他特征分析内容]
        最终结论：[必须使用方括号，这里写出情绪类型(只区分积极、中性、消极)]
        ""
        """
    response = client.chat.completions.create(
        model="ep-20250513173018-dm7j2",
        messages=[
            {"role": "system", "content": system_prompt},
            {"role": "user", "content": text}
        ],
        temperature=0.3
    )
    
    raw_result = response.choices[0].message.content
    return parse_emotion_result(raw_result)

def parse_emotion_result(raw_text):
    # 使用更精准的方括号提取法
    keyword_analysis = re.search(r'关键词分析：\[(.*?)\]', raw_text)
    tone_analysis = re.search(r'语气分析：\[(.*?)\]', raw_text)
    feature_analysis = re.search(r'其他特征分析：\[(.*?)\]', raw_text)
    emotion_type = re.search(r'最终结论：\[(.*?)\]', raw_text)
    
    # 构建结构化结果
    result = {
        "type": emotion_type.group(1).strip() if emotion_type else None,
        "keyword_analysis": keyword_analysis.group(1).strip() if keyword_analysis else None,
        "analysis_fields": {
            "tone_analysis": tone_analysis.group(1).strip() if tone_analysis else None,
            "feature_analysis": feature_analysis.group(1).strip() if feature_analysis else None
        }
    }
    
    return result


        
def save_to_file(data, filename):
    with open(filename, 'w', encoding='utf-8') as f:
        json.dump(data, f, indent=2, ensure_ascii=False)

def main():
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest='command', required=True)

    # 动物识别命令
    animal_parser = subparsers.add_parser('animal', help='动物识别分析')
    animal_parser.add_argument('--image-url', required=True, help='图片URL')

    # 情绪分析命令
    emotion_parser = subparsers.add_parser('emotion', help='情绪分析')
    emotion_parser.add_argument('--text', help='待分析文本')
    emotion_parser.add_argument('--text-base64', help='Base64编码的文本')

    args = parser.parse_args()

    try:
        if args.command == 'animal':
            result = analyze_animal(args.image_url)
            save_to_file(result, filename="animal_result.json")
        elif args.command == 'emotion':
            if args.text_base64:
                decoded_bytes = base64.b64decode(args.text_base64)
                decoded_text = decoded_bytes.decode('utf-8')
                result = analyze_emotion(decoded_text)
            elif args.text:
                result = analyze_emotion(args.text)
            else:
                raise ValueError("需要提供 --text 或 --text-base64 参数")
            save_to_file(result, filename="emotion_result.json")
    except Exception as e:
        error_result = {"error": str(e)}
        if args.command == 'animal':
            save_to_file(error_result, filename="animal_result.json")
        elif args.command == 'emotion':
            save_to_file(error_result, filename="emotion_result.json")
        exit(1)

if __name__ == '__main__':
    main()