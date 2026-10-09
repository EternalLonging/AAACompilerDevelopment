# 上下文消歧后的 LL(1) 预测分析核心

本核心有 124 个非终结符、267 条产生式，SELECT 冲突格为 0。
该结论仅适用于经过上下文分类的终结符流及本文 BNF；项目语言类似 C89，具体规则以本草案和后续项目规范为准；整体 Token 适配流程不属于纯 LL(1)。

## 必须执行的上下文与后置检查

1. TYPE_NAME：在类型名可能出现的位置查询 typedef 作用域。正在声明的名字、标签、成员名字位置保持 ID。提交声明后及时更新绑定，离开作用域恢复；不一次性预分类整个文件。
2. LABEL_ID：仅语句入口，当前为 ID 且下一 Token 为 COLON 时形成临时标签分类。原始 TokenType 不变。此适配步骤有两 Token 判断，整体不是纯原始 Token LL(1)。
3. AssignmentTail：先解析条件表达式，再处理赋值；若赋值左部不具有原文法 UnaryExpression 形状则语法拒绝。之后另做可修改左值语义检查。当前草案不接受 a+b=c；是否调整此约束须修改文法并重算集合。
4. ParameterDirect：记录声明符是否具名；无名字时拒绝 abstract-declarator 的旧式 ID 参数列表和无效纯分组空串。具名语法不允许仅有数组/函数后缀而缺少 ID。
5. TypeSpecifier/StorageClass/void 参数等合法组合、旧式函数参数对应关系、位域、常量表达式和控制语句位置按项目语义规范验证，C89 仅作为参考；现有候选构造不自动成为全部实现要求。
6. if、else、while、for、do、switch 的语句体必须使用花括号；当前草案块内先声明后语句，for 初始化不接受声明；这些具体规则可按项目需求调整。

## 文法 BNF

P001  TranslationUnit → ExternalDeclaration TranslationTail
P002  TranslationTail → ExternalDeclaration TranslationTail
P003  TranslationTail → ε
P004  ExternalDeclaration → DeclarationSpecifiers ExternalAfterSpecs
P005  ExternalDeclaration → Declarator OldDeclarations CompoundStatement
P006  ExternalAfterSpecs → SEMI
P007  ExternalAfterSpecs → Declarator ExternalDeclaratorTail
P008  ExternalDeclaratorTail → OldDeclarations CompoundStatement
P009  ExternalDeclaratorTail → InitializerOpt InitDeclaratorTail SEMI
P010  OldDeclarations → Declaration OldDeclarations
P011  OldDeclarations → ε
P012  Declaration → DeclarationSpecifiers InitDeclaratorsOpt SEMI
P013  DeclarationSpecifiers → DeclarationSpecifier DeclarationSpecifiersTail
P014  DeclarationSpecifiersTail → DeclarationSpecifier DeclarationSpecifiersTail
P015  DeclarationSpecifiersTail → ε
P016  DeclarationSpecifier → StorageClass
P017  DeclarationSpecifier → TypeSpecifier
P018  DeclarationSpecifier → TypeQualifier
P019  StorageClass → KW_TYPEDEF
P020  StorageClass → KW_EXTERN
P021  StorageClass → KW_STATIC
P022  StorageClass → KW_AUTO
P023  StorageClass → KW_REGISTER
P024  TypeQualifier → KW_CONST
P025  TypeQualifier → KW_VOLATILE
P026  TypeSpecifier → KW_VOID
P027  TypeSpecifier → KW_CHAR
P028  TypeSpecifier → KW_SHORT
P029  TypeSpecifier → KW_INT
P030  TypeSpecifier → KW_LONG
P031  TypeSpecifier → KW_FLOAT
P032  TypeSpecifier → KW_DOUBLE
P033  TypeSpecifier → KW_SIGNED
P034  TypeSpecifier → KW_UNSIGNED
P035  TypeSpecifier → StructUnionSpecifier
P036  TypeSpecifier → EnumSpecifier
P037  TypeSpecifier → TYPE_NAME
P038  StructUnionSpecifier → StructUnion StructUnionBody
P039  StructUnion → KW_STRUCT
P040  StructUnion → KW_UNION
P041  StructUnionBody → ID StructBodyOpt
P042  StructUnionBody → LBRACE StructDeclarations RBRACE
P043  StructBodyOpt → LBRACE StructDeclarations RBRACE
P044  StructBodyOpt → ε
P045  StructDeclarations → StructDeclaration StructDeclarationsTail
P046  StructDeclarationsTail → StructDeclaration StructDeclarationsTail
P047  StructDeclarationsTail → ε
P048  StructDeclaration → SpecifierQualifierList StructDeclaratorList SEMI
P049  SpecifierQualifierList → SpecifierQualifier SpecifierQualifierTail
P050  SpecifierQualifier → TypeSpecifier
P051  SpecifierQualifier → TypeQualifier
P052  SpecifierQualifierTail → SpecifierQualifier SpecifierQualifierTail
P053  SpecifierQualifierTail → ε
P054  StructDeclaratorList → StructDeclarator StructDeclaratorTail
P055  StructDeclaratorTail → COMMA StructDeclarator StructDeclaratorTail
P056  StructDeclaratorTail → ε
P057  StructDeclarator → Declarator BitFieldOpt
P058  StructDeclarator → COLON ConstantExpression
P059  BitFieldOpt → COLON ConstantExpression
P060  BitFieldOpt → ε
P061  EnumSpecifier → KW_ENUM EnumBody
P062  EnumBody → ID EnumBodyOpt
P063  EnumBody → LBRACE EnumeratorList RBRACE
P064  EnumBodyOpt → LBRACE EnumeratorList RBRACE
P065  EnumBodyOpt → ε
P066  EnumeratorList → Enumerator EnumeratorTail
P067  EnumeratorTail → COMMA Enumerator EnumeratorTail
P068  EnumeratorTail → ε
P069  Enumerator → ID EnumeratorValueOpt
P070  EnumeratorValueOpt → ASSIGN ConstantExpression
P071  EnumeratorValueOpt → ε
P072  InitDeclaratorsOpt → InitDeclarator InitDeclaratorTail
P073  InitDeclaratorsOpt → ε
P074  InitDeclarator → Declarator InitializerOpt
P075  InitDeclaratorTail → COMMA InitDeclarator InitDeclaratorTail
P076  InitDeclaratorTail → ε
P077  InitializerOpt → ASSIGN Initializer
P078  InitializerOpt → ε
P079  Initializer → AssignmentExpression
P080  Initializer → LBRACE InitializerList InitializerClose
P081  InitializerList → Initializer InitializerListTail
P082  InitializerListTail → COMMA InitializerAfterComma
P083  InitializerListTail → ε
P084  InitializerAfterComma → Initializer InitializerListTail
P085  InitializerAfterComma → ε
P086  InitializerClose → RBRACE
P087  Declarator → PointerOpt DirectDeclarator
P088  PointerOpt → Pointer
P089  PointerOpt → ε
P090  Pointer → STAR TypeQualifiersOpt PointerOpt
P091  TypeQualifiersOpt → TypeQualifier TypeQualifiersOpt
P092  TypeQualifiersOpt → ε
P093  DirectDeclarator → ID DeclaratorSuffixes
P094  DirectDeclarator → LPAREN Declarator RPAREN DeclaratorSuffixes
P095  DeclaratorSuffixes → DeclaratorSuffix DeclaratorSuffixes
P096  DeclaratorSuffixes → ε
P097  DeclaratorSuffix → LBRACKET ConstantExpressionOpt RBRACKET
P098  DeclaratorSuffix → LPAREN FunctionParametersOpt RPAREN
P099  ConstantExpressionOpt → ConstantExpression
P100  ConstantExpressionOpt → ε
P101  FunctionParametersOpt → ParameterTypeList
P102  FunctionParametersOpt → IdentifierList
P103  FunctionParametersOpt → ε
P104  IdentifierList → ID IdentifierTail
P105  IdentifierTail → COMMA ID IdentifierTail
P106  IdentifierTail → ε
P107  ParameterTypeList → ParameterDeclaration ParameterTail
P108  ParameterTail → COMMA ParameterAfterComma
P109  ParameterTail → ε
P110  ParameterAfterComma → ParameterDeclaration ParameterTail
P111  ParameterAfterComma → ELLIPSIS
P112  ParameterDeclaration → DeclarationSpecifiers ParameterDeclaratorOpt
P113  ParameterDeclaratorOpt → ParameterDeclaratorNonEmpty
P114  ParameterDeclaratorOpt → ε
P115  TypeName → SpecifierQualifierList AbstractDeclaratorOpt
P116  AbstractDeclaratorOpt → AbstractDeclarator
P117  AbstractDeclaratorOpt → ε
P118  AbstractDeclarator → Pointer DirectAbstractOpt
P119  AbstractDeclarator → DirectAbstractDeclarator
P120  DirectAbstractOpt → DirectAbstractDeclarator
P121  DirectAbstractOpt → ε
P122  DirectAbstractDeclarator → LPAREN AbstractParenBody RPAREN AbstractSuffixes
P123  DirectAbstractDeclarator → LBRACKET ConstantExpressionOpt RBRACKET AbstractSuffixes
P124  AbstractParenBody → AbstractDeclarator
P125  AbstractParenBody → ParameterTypeListOpt
P126  ParameterTypeListOpt → ParameterTypeList
P127  ParameterTypeListOpt → ε
P128  AbstractSuffixes → AbstractSuffix AbstractSuffixes
P129  AbstractSuffixes → ε
P130  AbstractSuffix → LBRACKET ConstantExpressionOpt RBRACKET
P131  AbstractSuffix → LPAREN ParameterTypeListOpt RPAREN
P132  CompoundStatement → LBRACE DeclarationsOpt StatementsOpt RBRACE
P133  DeclarationsOpt → Declaration DeclarationsOpt
P134  DeclarationsOpt → ε
P135  StatementsOpt → Statement StatementsOpt
P136  StatementsOpt → ε
P137  Statement → LabeledStatement
P138  Statement → CompoundStatement
P139  Statement → ExpressionStatement
P140  Statement → SelectionStatement
P141  Statement → IterationStatement
P142  Statement → JumpStatement
P143  LabeledStatement → LABEL_ID COLON Statement
P144  LabeledStatement → KW_CASE ConstantExpression COLON Statement
P145  LabeledStatement → KW_DEFAULT COLON Statement
P146  ExpressionStatement → ExpressionOpt SEMI
P147  ExpressionOpt → Expression
P148  ExpressionOpt → ε
P149  SelectionStatement → KW_IF LPAREN Expression RPAREN CompoundStatement ElseOpt
P150  SelectionStatement → KW_SWITCH LPAREN Expression RPAREN CompoundStatement
P151  ElseOpt → KW_ELSE CompoundStatement
P152  ElseOpt → ε
P153  IterationStatement → KW_WHILE LPAREN Expression RPAREN CompoundStatement
P154  IterationStatement → KW_DO CompoundStatement KW_WHILE LPAREN Expression RPAREN SEMI
P155  IterationStatement → KW_FOR LPAREN ExpressionOpt SEMI ExpressionOpt SEMI ExpressionOpt RPAREN CompoundStatement
P156  JumpStatement → KW_GOTO ID SEMI
P157  JumpStatement → KW_CONTINUE SEMI
P158  JumpStatement → KW_BREAK SEMI
P159  JumpStatement → KW_RETURN ExpressionOpt SEMI
P160  Expression → AssignmentExpression ExpressionTail
P161  ExpressionTail → COMMA AssignmentExpression ExpressionTail
P162  ExpressionTail → ε
P163  AssignmentExpression → ConditionalExpression AssignmentTail
P164  AssignmentOperator → ASSIGN
P165  AssignmentOperator → PLUS_ASSIGN
P166  AssignmentOperator → MINUS_ASSIGN
P167  AssignmentOperator → STAR_ASSIGN
P168  AssignmentOperator → SLASH_ASSIGN
P169  AssignmentOperator → PERCENT_ASSIGN
P170  AssignmentOperator → SHIFT_LEFT_ASSIGN
P171  AssignmentOperator → SHIFT_RIGHT_ASSIGN
P172  AssignmentOperator → AND_ASSIGN
P173  AssignmentOperator → OR_ASSIGN
P174  AssignmentOperator → XOR_ASSIGN
P175  ConstantExpression → ConditionalExpression
P176  ConditionalExpression → LogicalOrExpression ConditionalTail
P177  ConditionalTail → QUESTION Expression COLON ConditionalExpression
P178  ConditionalTail → ε
P179  LogicalOrExpression → LogicalAndExpression LogicalOrTail
P180  LogicalOrTail → OR LogicalAndExpression LogicalOrTail
P181  LogicalOrTail → ε
P182  LogicalAndExpression → BitwiseOrExpression LogicalAndTail
P183  LogicalAndTail → AND BitwiseOrExpression LogicalAndTail
P184  LogicalAndTail → ε
P185  BitwiseOrExpression → BitwiseXorExpression BitwiseOrTail
P186  BitwiseOrTail → BIT_OR BitwiseXorExpression BitwiseOrTail
P187  BitwiseOrTail → ε
P188  BitwiseXorExpression → BitwiseAndExpression BitwiseXorTail
P189  BitwiseXorTail → BIT_XOR BitwiseAndExpression BitwiseXorTail
P190  BitwiseXorTail → ε
P191  BitwiseAndExpression → EqualityExpression BitwiseAndTail
P192  BitwiseAndTail → AMP EqualityExpression BitwiseAndTail
P193  BitwiseAndTail → ε
P194  EqualityExpression → RelationalExpression EqualityTail
P195  EqualityTail → EQ RelationalExpression EqualityTail
P196  EqualityTail → NE RelationalExpression EqualityTail
P197  EqualityTail → ε
P198  RelationalExpression → ShiftExpression RelationalTail
P199  RelationalTail → LT ShiftExpression RelationalTail
P200  RelationalTail → GT ShiftExpression RelationalTail
P201  RelationalTail → LE ShiftExpression RelationalTail
P202  RelationalTail → GE ShiftExpression RelationalTail
P203  RelationalTail → ε
P204  ShiftExpression → AdditiveExpression ShiftTail
P205  ShiftTail → SHIFT_LEFT AdditiveExpression ShiftTail
P206  ShiftTail → SHIFT_RIGHT AdditiveExpression ShiftTail
P207  ShiftTail → ε
P208  AdditiveExpression → MultiplicativeExpression AdditiveTail
P209  AdditiveTail → PLUS MultiplicativeExpression AdditiveTail
P210  AdditiveTail → MINUS MultiplicativeExpression AdditiveTail
P211  AdditiveTail → ε
P212  MultiplicativeExpression → CastExpression MultiplicativeTail
P213  MultiplicativeTail → STAR CastExpression MultiplicativeTail
P214  MultiplicativeTail → SLASH CastExpression MultiplicativeTail
P215  MultiplicativeTail → PERCENT CastExpression MultiplicativeTail
P216  MultiplicativeTail → ε
P217  CastExpression → LPAREN CastParenTail
P218  CastExpression → NonParenUnary
P219  UnaryExpression → LPAREN Expression RPAREN PostfixTail
P220  UnaryExpression → NonParenUnary
P221  UnaryOperator → AMP
P222  UnaryOperator → STAR
P223  UnaryOperator → PLUS
P224  UnaryOperator → MINUS
P225  UnaryOperator → BIT_NOT
P226  UnaryOperator → NOT
P227  SizeofOperand → LPAREN SizeofParenTail
P228  SizeofOperand → NonParenUnary
P229  PostfixTail → LBRACKET Expression RBRACKET PostfixTail
P230  PostfixTail → LPAREN ArgumentsOpt RPAREN PostfixTail
P231  PostfixTail → DOT ID PostfixTail
P232  PostfixTail → ARROW ID PostfixTail
P233  PostfixTail → PLUS_PLUS PostfixTail
P234  PostfixTail → MINUS_MINUS PostfixTail
P235  PostfixTail → ε
P236  ArgumentsOpt → AssignmentExpression ArgumentTail
P237  ArgumentsOpt → ε
P238  ArgumentTail → COMMA AssignmentExpression ArgumentTail
P239  ArgumentTail → ε
P240  StringSequence → STRING_LITERAL StringSequenceTail
P241  StringSequenceTail → STRING_LITERAL StringSequenceTail
P242  StringSequenceTail → ε
P243  AssignmentTail → AssignmentOperator AssignmentExpression
P244  AssignmentTail → ε
P245  CastParenTail → TypeName RPAREN CastExpression
P246  CastParenTail → Expression RPAREN PostfixTail
P247  NonParenUnary → Atom PostfixTail
P248  NonParenUnary → PLUS_PLUS UnaryExpression
P249  NonParenUnary → MINUS_MINUS UnaryExpression
P250  NonParenUnary → UnaryOperator CastExpression
P251  NonParenUnary → KW_SIZEOF SizeofOperand
P252  Atom → ID
P253  Atom → INT_LITERAL
P254  Atom → FLOAT_LITERAL
P255  Atom → CHAR_LITERAL
P256  Atom → StringSequence
P257  SizeofParenTail → TypeName RPAREN
P258  SizeofParenTail → Expression RPAREN PostfixTail
P259  ParameterDeclaratorNonEmpty → Pointer ParameterDirectOpt
P260  ParameterDeclaratorNonEmpty → ParameterDirect
P261  ParameterDirectOpt → ParameterDirect
P262  ParameterDirectOpt → ε
P263  ParameterDirect → ID DeclaratorSuffixes
P264  ParameterDirect → LPAREN ParameterParenBody RPAREN DeclaratorSuffixes
P265  ParameterDirect → LBRACKET ConstantExpressionOpt RBRACKET DeclaratorSuffixes
P266  ParameterParenBody → ParameterDeclaratorNonEmpty
P267  ParameterParenBody → ParameterTypeListOpt

## 解析动作约定

乘除、加减等尾部以循环按左结合折叠 AST；赋值与条件表达式按右结合构造。空产生式不消费 Token。
更新 typedef 绑定与 AST 构造等动作由实现补充；本次交付集合、分析表和流程设计，未实现正式 C++ 解析器。