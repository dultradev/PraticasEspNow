import json
import boto3
from datetime import datetime
from decimal import Decimal

def lambda_handler(event, context):
    # Headers CORS para todas as respostas
    cors_headers = {
        'Content-Type': 'application/json',
        'Access-Control-Allow-Origin': '*',
        'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
        'Access-Control-Allow-Headers': 'Content-Type'
    }
    
    try:
        # Log do evento para debug
        print(f"Event recebido: {json.dumps(event)}")
        
        # Tratar requisições OPTIONS (preflight CORS)
        if event.get('requestContext', {}).get('http', {}).get('method') == 'OPTIONS':
            return {
                'statusCode': 200,
                'headers': cors_headers,
                'body': ''
            }
        
        # Inicializar DynamoDB
        dynamodb = boto3.resource('dynamodb', region_name='INSIRA O NOME DA REGIÃO QUE O DYNAMODB ESTÁ ALOCADO AQUI')
        table = dynamodb.Table('INSIRA O NOME DA SUA TABELA AQUI')
        
        # Buscar dados
        print("Buscando dados do DynamoDB...")
        response = table.scan(Limit=100)
        items = response.get('Items', [])
        
        print(f"Encontrados {len(items)} itens")
        
        # Converter Decimal para tipos JSON compatíveis
        def converter_decimals(obj):
            if isinstance(obj, list):
                return [converter_decimals(item) for item in obj]
            elif isinstance(obj, dict):
                return {key: converter_decimals(value) for key, value in obj.items()}
            elif isinstance(obj, Decimal):
                if obj % 1 == 0:
                    return int(obj)
                else:
                    return float(obj)
            else:
                return obj
        
        # Processar dados
        items_convertidos = converter_decimals(items)
        
        # Ordenar por timestamp se existir
        try:
            items_ordenados = sorted(
                items_convertidos, 
                key=lambda x: x.get('timestamp', ''), 
                reverse=True
            )
        except:
            items_ordenados = items_convertidos
        
        # Preparar resposta
        dados_dashboard = {
            'total_records': len(items_convertidos),
            'latest_data': items_ordenados[:10],
            'timestamp': datetime.now().isoformat(),
            'status': 'success'
        }
        
        print("Dados processados com sucesso")
        
        return {
            'statusCode': 200,
            'headers': cors_headers,
            'body': json.dumps(dados_dashboard, ensure_ascii=False)
        }
        
    except Exception as erro:
        print(f"ERRO na Lambda: {str(erro)}")
        print(f"Tipo do erro: {type(erro)}")
        
        # Resposta de erro com CORS
        return {
            'statusCode': 500,
            'headers': cors_headers,
            'body': json.dumps({
                'error': str(erro),
                'status': 'error',
                'total_records': 0,
                'latest_data': [],
                'timestamp': datetime.now().isoformat()
            })
        }
