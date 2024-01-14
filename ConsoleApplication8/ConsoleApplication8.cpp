#include "pch.h"
using namespace System;
using namespace System::Data::SqlClient;
using namespace System::Net;
using namespace System::Net::Sockets;
using namespace System::Text;
using namespace System::Collections::Generic;
ref class Server {
private:
    TcpListener^ tcpListener;
    TcpClient^ tcpClient;

    void SendResponse(NetworkStream^ networkStream, String^ response) {
        array<Byte>^ responseBytes = Encoding::UTF8->GetBytes(response);
        networkStream->Write(responseBytes, 0, responseBytes->Length);
    }
    //files func
    void HandleSendFileRequest(NetworkStream^ networkStream, String^ chatName, String^ username, array<Byte>^ fileData, String^ fileName) {
        Console::WriteLine("Started processing file send request for user '{0}' and file '{1}'.", username, fileName);
        int userId = GetUserIdByUsername(username);
        if (userId == -1) {
            SendResponse(networkStream, "User not found");
            return;
        }
        String^ connectionString = "Data Source=(localdb)\\MSSQLLocalDB;Initial Catalog=chat;Integrated Security=True;";
        SqlConnection^ connection = gcnew SqlConnection(connectionString);
        try {
            connection->Open();

            String^ query = "INSERT INTO Messages (ChatName, UserID, FileName, FileData, MessageText, Timestamp, FileSize) VALUES (@chatName, @userId, @fileName, @fileData, 'file', @timestamp, @fileSize)";
            SqlCommand^ command = gcnew SqlCommand(query, connection);
            command->Parameters->AddWithValue("@chatName", chatName);
            command->Parameters->AddWithValue("@userId", userId);
            command->Parameters->AddWithValue("@fileName", fileName);

            // Используйте VARBINARY(MAX) для хранения бинарных данных файла
            SqlParameter^ fileDataParam = gcnew SqlParameter("@fileData", System::Data::SqlDbType::VarBinary, -1);
            fileDataParam->Value = fileData;
            command->Parameters->Add(fileDataParam);

            command->Parameters->AddWithValue("@timestamp", DateTime::Now);

            // Добавляем размер файла в параметры команды
            int fileSize = fileData->Length;
            command->Parameters->AddWithValue("@fileSize", fileSize);

            command->ExecuteNonQuery();

            // Отправьте размер файла клиенту перед отправкой файла
            String^ fileInfo = String::Format("{0}:{1}", fileName, fileSize);
            array<Byte>^ fileInfoBytes = Encoding::UTF8->GetBytes(fileInfo);
            networkStream->Write(fileInfoBytes, 0, fileInfoBytes->Length);

            // Отправка файла частями
            const int bufferSize = 1024; // Размер буфера
            for (int i = 0; i < fileSize; i += bufferSize) {
                int chunkSize = Math::Min(bufferSize, fileSize - i);
                networkStream->Write(fileData, i, chunkSize);
            }

            // Добавьте информацию о размере файла в логи
            Console::WriteLine("File '{0}' sent successfully. Size: {1} bytes", fileName, fileSize);

            SendResponse(networkStream, "File sent successfully");
        }
        catch (Exception^ ex) {
            Console::WriteLine("Error: " + ex->Message);
            SendResponse(networkStream, "Error sending file");
        }
        finally {
            connection->Close();
            Console::WriteLine("Completed processing file send request for user '{0}' and file '{1}'.", username, fileName);
        }
    }




    void HandleReceiveFileRequest(NetworkStream^ networkStream, String^ fileName, String^ chatName) {
        Console::WriteLine("Started processing file receive request for chat.", chatName);
        String^ connectionString = "Data Source=(localdb)\\MSSQLLocalDB;Initial Catalog=chat;Integrated Security=True;";
        SqlConnection^ connection = gcnew SqlConnection(connectionString);
        try {
            connection->Open();

            String^ query = "SELECT FileName, FileData FROM Messages WHERE ChatName = @chatName AND FileName = @fileName";
            SqlCommand^ command = gcnew SqlCommand(query, connection);
            command->Parameters->AddWithValue("@chatName", chatName);
            command->Parameters->AddWithValue("@fileName", fileName);

            SqlDataReader^ reader = command->ExecuteReader();
            if (reader->Read()) {
                String^ fileName = reader->GetString(0);
                array<Byte>^ fileData = (array<Byte>^)reader["FileData"];
                int fileSize = fileData->Length;

                // Проверка размера файла перед отправкой (не более 20 МБ)
                if (fileSize <= 20 * 1024 * 1024) { // 20 МБ в байтах
                    // Отправить клиенту имя файла и размер файла
                    // Отправка информации о файле
                    String^ fileInfo = String::Format("{0}:{1}", fileName, fileSize);
                    array<Byte>^ fileInfoBytes = Encoding::UTF8->GetBytes(fileInfo);
                    networkStream->Write(fileInfoBytes, 0, fileInfoBytes->Length);
                    Console::WriteLine(fileInfo);

                    // Отправка всего файла сразу
                    networkStream->Write(fileData, 0, fileSize);
                }
                else {
                    Console::WriteLine("File size exceeds the limit (20 MB).");
                    SendResponse(networkStream, "File size exceeds the limit (20 MB)");
                }
            }
        }
        catch (Exception^ ex) {
            Console::WriteLine("Error: " + ex->Message);
            SendResponse(networkStream, "Error receiving file");
        }
        finally {
            connection->Close();
            Console::WriteLine("Completed processing file receive request for chat '{0}'.", chatName);
        }
    }




    void HandleGetChatMessagesRequest(NetworkStream^ networkStream, String^ chatName, String^ currentUser) {
        String^ connectionString = "Data Source=(localdb)\\MSSQLLocalDB;Initial Catalog=chat;Integrated Security=True;";
        try {
            SqlConnection^ connection = gcnew SqlConnection(connectionString);
            connection->Open();

            int currentUserId = GetUserIdByUsername(currentUser);

            // Обновленный запрос для получения текстовых сообщений и информации о файлах
            String^ query = "SELECT u.Username, m.Timestamp, m.MessageText, m.FileName, m.FileSize FROM Messages m INNER JOIN Users u ON m.UserID = u.UserID WHERE m.ChatName = @chatName ORDER BY m.Timestamp";
            SqlCommand^ command = gcnew SqlCommand(query, connection);
            command->Parameters->AddWithValue("@chatName", chatName);

            SqlDataReader^ reader = command->ExecuteReader();

            StringBuilder^ messagesBuilder = gcnew StringBuilder();
            while (reader->Read()) {
                String^ username = reader->GetString(0);
                DateTime timestamp = reader->GetDateTime(1);
                String^ messageText = reader->GetString(2);
                String^ fileName = reader->IsDBNull(3) ? "" : reader->GetString(3);
                int fileSize = reader->IsDBNull(4) ? 0 : reader->GetInt32(4); // Получение размера файла

                String^ messageDisplay;
                if (username == currentUser) {
                    messageDisplay = "You";
                }
                else {
                    messageDisplay += username;
                }

                String^ formattedTime = timestamp.ToString("yyyy-MM-dd HH:mm:ss");

                // Формирование строки сообщения
                String^ displayMessage;
                if (!String::IsNullOrEmpty(fileName)) {
                    displayMessage = String::Format("file:{0} ({1}) [{2} bytes]: {3}\n", messageDisplay, formattedTime, fileSize, fileName);
                }
                else {
                    displayMessage = String::Format("message:{0} ({1}): {2}\n", messageDisplay, formattedTime, messageText);
                }

                messagesBuilder->Append(displayMessage);
            }

            // Логирование размера файла и названия файла перед отправкой списка сообщений
            Console::WriteLine(messagesBuilder->ToString());

            SendResponse(networkStream, messagesBuilder->ToString());

            reader->Close();
            connection->Close();
        }
        catch (Exception^ ex) {
            Console::WriteLine("Error: " + ex->Message);
            String^ errorMessage = "chat_messages_response:Error retrieving chat messages";
            SendResponse(networkStream, errorMessage);
        }
    }




    // Функция для получения UserID по имени пользователя из базы данных
    int GetUserIdByUsername(String^ username) {
        int userId = -1;  // Инициализируем значение -1, чтобы указать, что пользователя не найдено

        String^ connectionString = "Data Source=(localdb)\\MSSQLLocalDB;Initial Catalog=chat;Integrated Security=True;";

        try {
            SqlConnection^ connection = gcnew SqlConnection(connectionString);
            connection->Open();

            // Запрос для выбора UserID по имени пользователя
            String^ query = "SELECT UserID FROM Users WHERE Username = @username";
            SqlCommand^ command = gcnew SqlCommand(query, connection);
            command->Parameters->AddWithValue("@username", username);

            // Выполнение запроса
            Object^ result = command->ExecuteScalar();

            // Проверка, было ли получено значение
            if (result != nullptr) {
                userId = Convert::ToInt32(result);
            }

            connection->Close();
        }
        catch (Exception^ ex) {
            Console::WriteLine("Error: " + ex->Message);
            // Обработка ошибки при подключении к базе данных или выполнении запроса
        }

        return userId;
    }
    // В функции HandleSendMessageRequest добавьте вызов HandleGetChatMessagesRequest для обновления сообщений после отправки нового сообщения
    void HandleSendMessageRequest(NetworkStream^ networkStream, String^ requestData) {
        array<String^>^ messageData = requestData->Split(':');
        if (messageData->Length == 4 && messageData[0] == "send_message_request") {
            String^ chatName = messageData[1];
            String^ sender = messageData[2];
            String^ messageText = messageData[3];
            DateTime timestamp = DateTime::Now;

            String^ connectionString = "Data Source=(localdb)\\MSSQLLocalDB;Initial Catalog=chat;Integrated Security=True;";
            try {
                SqlConnection^ connection = gcnew SqlConnection(connectionString);
                connection->Open();

                // Получить UserID по имени пользователя
                int userId = GetUserIdByUsername(sender);

                if (userId != -1) {
                    // Если пользователь существует, выполнить вставку
                    String^ insertMessageQuery = "INSERT INTO Messages (ChatName, UserID, MessageText, Timestamp) VALUES (@chatName, @UserID, @messageText, @timestamp)";
                    SqlCommand^ insertMessageCommand = gcnew SqlCommand(insertMessageQuery, connection);
                    insertMessageCommand->Parameters->AddWithValue("@chatName", chatName);
                    insertMessageCommand->Parameters->AddWithValue("@UserID", userId);
                    insertMessageCommand->Parameters->AddWithValue("@messageText", messageText);
                    insertMessageCommand->Parameters->AddWithValue("@timestamp", timestamp);

                    insertMessageCommand->ExecuteNonQuery();

                    String^ response = "Message sent successfully";
                    SendResponse(networkStream, response);
                    Console::WriteLine("Message sent successfully");
                }
                else {
                    // Если пользователь не существует, обработать ошибку
                    String^ response = "Sender not found";
                    SendResponse(networkStream, response);
                    Console::WriteLine("Sender not found");
                }

                connection->Close();
            }
            catch (Exception^ ex) {
                Console::WriteLine("Error: " + ex->Message);
                String^ response = "Error sending message";
                SendResponse(networkStream, response);
            }
        }
        else {
            String^ response = "Invalid send message request format";
            SendResponse(networkStream, response);
            Console::WriteLine("Invalid send message request format");
        }
    }
    void HandleGetChatListRequest(NetworkStream^ networkStream, String^ value) {
        // Подготовка списка чатов для отправки на клиент
        String^ chatList;

        String^ connectionString = "Data Source=(localdb)\\MSSQLLocalDB;Initial Catalog=chat;Integrated Security=True;";
        try {
            SqlConnection^ connection = gcnew SqlConnection(connectionString);
            connection->Open();

            // Замените "User1" и "User2" на соответствующие поля в вашей таблице чатов
            String^ query = "SELECT ChatName FROM Chats WHERE ChatName LIKE '%' + @value + '%'";
            SqlCommand^ command = gcnew SqlCommand(query, connection);
            command->Parameters->AddWithValue("@value", value);

            SqlDataReader^ reader = command->ExecuteReader();

            List<String^> chatNames;
            while (reader->Read()) {
                chatNames.Add(reader->GetString(0));
            }

            array<Object^>^ chatNamesArray = chatNames.ToArray();
            chatList = String::Join(",", chatNamesArray);

            reader->Close();
            connection->Close();
        }
        catch (Exception^ ex) {
            Console::WriteLine("Error: " + ex->Message);
            // Добавьте обработку ошибок при подключении к базе данных или выполнении запроса
            chatList = "chat_list_response:Error retrieving chat list";
        }

        // Подготовка списка чатов для отправки на клиент
        SendResponse(networkStream, chatList);
        Console::WriteLine("Chat list sent to client");
    }

    void HandleCreateChatRequest(NetworkStream^ networkStream, String^ requestData) {
        array<String^>^ createChatData = requestData->Split(':');
        if (createChatData->Length == 2 && createChatData[0] == "create_chat_request") {
            String^ newChatName = createChatData[1];

            String^ connectionString = "Data Source=(localdb)\\MSSQLLocalDB;Initial Catalog=chat;Integrated Security=True;";
            try {
                SqlConnection^ connection = gcnew SqlConnection(connectionString);
                connection->Open();

                // Проверим, существует ли чат с таким именем
                String^ checkChatQuery = "SELECT COUNT(*) FROM Chats WHERE ChatName = @ChatName";
                SqlCommand^ checkChatCommand = gcnew SqlCommand(checkChatQuery, connection);
                checkChatCommand->Parameters->AddWithValue("@ChatName", newChatName);

                int existingChatCount = (int)checkChatCommand->ExecuteScalar();

                if (existingChatCount > 0) {
                    String^ response = "Chat with this name already exists";
                    SendResponse(networkStream, response);
                    Console::WriteLine("Chat with this name already exists");
                }
                else {
                    // Создадим новый чат
                    String^ createChatQuery = "INSERT INTO Chats (ChatName) VALUES (@ChatName)";
                    SqlCommand^ createChatCommand = gcnew SqlCommand(createChatQuery, connection);
                    createChatCommand->Parameters->AddWithValue("@ChatName", newChatName);

                    createChatCommand->ExecuteNonQuery();

                    String^ response = "Chat created successfully";
                    SendResponse(networkStream, response);
                    Console::WriteLine("Chat created successfully");
                }

                connection->Close();
            }
            catch (Exception^ ex) {
                Console::WriteLine("Error: " + ex->Message);
                String^ response = "Error during chat creation";
                SendResponse(networkStream, response);
            }
        }
        else {
            String^ response = "Invalid create chat request format";
            SendResponse(networkStream, response);
            Console::WriteLine("Invalid create chat request format");
        }
    }
    void HandleLoginRequest(NetworkStream^ networkStream, String^ requestData) {
        array<String^>^ loginData = requestData->Split(':');
        if (loginData->Length == 3 && loginData[0] == "login_request") {
            String^ username = loginData[1];
            String^ password = loginData[2];

            String^ connectionString = "Data Source=(localdb)\\MSSQLLocalDB;Initial Catalog=chat;Integrated Security=True;";

            try {
                SqlConnection^ connection = gcnew SqlConnection(connectionString);
                connection->Open();

                String^ query = "SELECT COUNT(*) FROM Users WHERE Username = @username AND Password = @password";
                SqlCommand^ command = gcnew SqlCommand(query, connection);
                command->Parameters->AddWithValue("@username", username);
                command->Parameters->AddWithValue("@password", password);

                int result = (int)command->ExecuteScalar();
                connection->Close();

                if (result > 0) {
                    String^ response = "Login successful";
                    SendResponse(networkStream, response);
                    Console::WriteLine("Login successful");
                }
                else {
                    String^ response = "Login failed";
                    SendResponse(networkStream, response);
                    Console::WriteLine("Login failed");
                }
            }
            catch (Exception^ ex) {
                Console::WriteLine("Error: " + ex->Message);
                String^ response = "Error during login";
                SendResponse(networkStream, response);
            }
        }
        else {
            String^ response = "Invalid login request format";
            SendResponse(networkStream, response);
        }
    }

    void HandleRegistrationRequest(NetworkStream^ networkStream, String^ requestData) {
        array<String^>^ registrationData = requestData->Split(':');
        if (registrationData->Length == 3 && registrationData[0] == "register_request") {
            String^ username = registrationData[1];
            String^ password = registrationData[2];

            String^ connectionString = "Data Source=(localdb)\\MSSQLLocalDB;Initial Catalog=chat;Integrated Security=True;";
            try {
                SqlConnection^ connection = gcnew SqlConnection(connectionString);
                connection->Open();

                String^ checkUserQuery = "SELECT COUNT(*) FROM Users WHERE Username = @username";
                SqlCommand^ checkUserCommand = gcnew SqlCommand(checkUserQuery, connection);
                checkUserCommand->Parameters->AddWithValue("@username", username);

                int existingUserCount = (int)checkUserCommand->ExecuteScalar();

                if (existingUserCount > 0) {
                    String^ response = "Username already exists";
                    SendResponse(networkStream, response);
                    Console::WriteLine("Username already exists");
                }
                else {
                    String^ registerQuery = "INSERT INTO Users (Username, Password) VALUES (@username, @password)";
                    SqlCommand^ registerCommand = gcnew SqlCommand(registerQuery, connection);
                    registerCommand->Parameters->AddWithValue("@username", username);
                    registerCommand->Parameters->AddWithValue("@password", password);

                    registerCommand->ExecuteNonQuery();

                    String^ response = "Registration successful";
                    SendResponse(networkStream, response);
                    Console::WriteLine("Registration successful");
                }

                connection->Close();
            }
            catch (Exception^ ex) {
                Console::WriteLine("Error: " + ex->Message);
                String^ response = "Error during registration";
                SendResponse(networkStream, response);

            }
        }
        else {
            String^ response = "Invalid registration request format";
            SendResponse(networkStream, response);
            Console::WriteLine("Invalid registration request format");
        }
    }

public:
    //запуск сервера и обработка полученных запросов
    Server() {
        tcpListener = gcnew TcpListener(IPAddress::Parse("127.0.0.1"), 1234);
        tcpListener->Start();

        Console::WriteLine("Server is listening on port 1234...");

        while (true) {
            tcpClient = tcpListener->AcceptTcpClient();
            Console::WriteLine("Client connected.");

            NetworkStream^ networkStream = tcpClient->GetStream();
            array<Byte>^ bytes = gcnew array<Byte>(1024);
            int bytesRead = networkStream->Read(bytes, 0, bytes->Length);

            String^ requestData = Encoding::UTF8->GetString(bytes, 0, bytesRead);
            Console::WriteLine("Received: {0}", requestData);

            if (requestData->StartsWith("login_request")) {
                HandleLoginRequest(networkStream, requestData);

            }
            else if (requestData->StartsWith("register_request")) {
                HandleRegistrationRequest(networkStream, requestData);
            }
            else if (requestData->StartsWith("create_chat_request")) 
            {
                HandleCreateChatRequest(networkStream, requestData);
            }
            else if (requestData->StartsWith("get_chat_list_request")) {
                // Получите имя пользователя из запроса
                array<String^>^ requestParts = requestData->Split(':');
                if (requestParts->Length == 2) {
                    String^ value = requestParts[1];
                    HandleGetChatListRequest(networkStream, value);
                }
                else {
                    String^ response = "Invalid get chat list request format";
                    SendResponse(networkStream, response);
                    Console::WriteLine("Invalid get chat list request format");
                }
            }
            else if (requestData->StartsWith("send_message_request")) {
                HandleSendMessageRequest(networkStream, requestData);
            }
            else if (requestData->StartsWith("get_chat_history_request:"))
            {
array<String^>^ requestParts = requestData->Split(':');
				if (requestParts->Length == 3) {
					String^ chatName = requestParts[1];
					String^ currentUser = requestParts[2];
					HandleGetChatMessagesRequest(networkStream, chatName, currentUser);
				}
				else {
					String^ response = "Invalid get chat history request format";
					SendResponse(networkStream, response);
					Console::WriteLine("Invalid get chat history request format");
				}
			}
            else if (requestData->StartsWith("send_file_request")) {
                // Разбиение строки запроса на части
                array<String^>^ parts = requestData->Split(':');
                if (parts->Length >= 4) {
                    String^ chatName = parts[1];
                    String^ username = parts[2];
                    String^ fileName = parts[3];

                    // Предполагается, что файловые данные следуют непосредственно за заголовком запроса
                    // Необходимо собрать оставшиеся части как данные файла
                    array<Byte>^ fileData = gcnew array<Byte>(bytesRead - (chatName->Length + username->Length + fileName->Length + 4));
                    Array::Copy(bytes, chatName->Length + username->Length + fileName->Length + 4, fileData, 0, fileData->Length);

                    HandleSendFileRequest(networkStream, chatName, username, fileData, fileName);
                }
                else {
                    SendResponse(networkStream, "Invalid send file request format");
                }
            }
            else if (requestData->StartsWith("get_file_request")) {
                // Разбиение строки запроса на части
                array<String^>^ parts = requestData->Split(':');
                if (parts->Length == 3) {
                    String^ fileName = parts[1];
                    String^ chatName = parts[2];
                    HandleReceiveFileRequest(networkStream, fileName, chatName);
                }
                else {
                    SendResponse(networkStream, "Invalid get file request format");
                }
            }
			else {
				String^ response = "Invalid request format";
				SendResponse(networkStream, response);
				Console::WriteLine("Invalid request format");
            }
            tcpClient->Close();
        }
    }
};

int main() {
    Server^ server = gcnew Server();
    return 0;
}
