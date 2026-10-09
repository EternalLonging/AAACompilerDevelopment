"""Render saved XLSX text without the artifact renderer's numeric coercion."""
from pathlib import Path
import openpyxl
from PIL import Image, ImageDraw, ImageFont

root = Path(__file__).resolve().parent
book = openpyxl.load_workbook(root.parent / 'AAACompilerDevelopment_词法状态转换表.xlsx',data_only=True)
font_path = Path('C:/Windows/Fonts/msyh.ttc')
assert font_path.exists()
font = ImageFont.truetype(str(font_path),20)
bold = ImageFont.truetype('C:/Windows/Fonts/msyhbd.ttc',22)
def render(sheet, rows, columns, widths, title, target):
    row_height=62
    image=Image.new('RGB',(sum(widths)+60,len(rows)*row_height+112),'white')
    draw=ImageDraw.Draw(image)
    draw.text((25,25),title,font=bold,fill='#203864')
    for r,source_row in enumerate(rows):
        x=25
        for col,width in zip(columns,widths):
            val=sheet.cell(source_row,col).value
            text='' if val is None else str(val)
            if r==0:
                fill,color='#203864','white'
            else:
                fill,color=('#F1F5FA' if r%2 else 'white'),'#203047'
            y=88+r*row_height
            draw.rectangle((x,y,x+width,y+row_height),fill=fill)
            assert draw.textlength(text,font=font)<=width-20,(source_row,col,text)
            draw.text((x+10,y+18),text,font=font,fill=color)
            x+=width
    image.save(root / target)
render(book.worksheets[2],range(79,86),[5,6,7],[320,350,280],
       '导出文件保存值核对：源码拼写','preview_saved_examples.png')
render(book.worksheets[1],range(7,16),[1,2,3,4],[140,690,460,110],
       '导出文件保存值核对：字节分类','preview_saved_classes.png')
print('Saved text previews generated.')
