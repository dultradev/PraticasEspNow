import json
import boto3 # Biblioteca criada pela AWS para manipulação de dados e serviços AWS em Python 

def lambda_handler(event, context):

    # Cria a conexão com o DynamoDB
    client = boto3.client('dynamodb')

    # Salva os dados recebidos do ESP32 na tabela
    response = client.put_item(
        TableName = '[INSIRA O NOME DA TABELA AQUI]',  
        # Nome da tabela criada no DynamoDB

        # JSON que está sendo enviado pelo ESP32. Tem que estar exatamente igual para dar certo
        Item = {
            "ID"           : {'N': event['ID']},
            "timestamp"    : {'S': event['timestamp']},
            "nivel_tinta"  : {'N': str(event['nivel_tinta'])},   
            "temperatura_c": {'N': str(event['temperatura_c'])},
            "umidade_pct"  : {'N': str(event['umidade_pct'])},
            "presenca"     : {'N': str(event['presenca'])}
        }
    )

    return 0