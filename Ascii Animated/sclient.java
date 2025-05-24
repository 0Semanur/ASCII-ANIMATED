import java.io.*;
import java.net.*;
import javax.swing.*;

public class sclient {
    // Metin alanı GUI'de görüntülemek için kullanılır
    private static JTextArea textArea;

    public static void main(String[] args) {
        // Argümanlar eksikse kullanım bilgisi gösterilir
        if (args.length < 6) {
            System.out.println("Kullanım: java sclient -a address -p port -ch channelID");
            return;
        }

        // Varsayılan değerler
        String address = "127.0.0.1";
        int port = 12345;
        int channelID = 0;

        // Komut satırı argümanlarını çözümleme
        for (int i = 0; i < args.length; i++) {
            if (args[i].equals("-a") && i + 1 < args.length) {
                address = args[++i];
            } else if (args[i].equals("-p") && i + 1 < args.length) {
                port = Integer.parseInt(args[++i]);
            } else if (args[i].equals("-ch") && i + 1 < args.length) {
                channelID = Integer.parseInt(args[++i]);
            }
        }

        // GUI oluşturma işlemini EDT (Event Dispatch Thread) üzerinde çalıştır
        SwingUtilities.invokeLater(() -> createAndShowGUI());

        // Sunucuya bağlanma ve veri alma işlemi
        try (Socket socket = new Socket(address, port);
             DataOutputStream out = new DataOutputStream(socket.getOutputStream());
             InputStream in = socket.getInputStream()) {

            // Seçilen kanal ID’si sunucuya gönderilir
            out.writeInt(channelID);
            System.out.println("Bağlanıldı. GUI penceresi açıldı, çıkmak için pencereyi kapatın.");

            // Sonsuz döngü ile gelen ASCII kareler okunur ve ekrana yazdırılır
            while (true) {
                StringBuilder frameBuilder = new StringBuilder();
                byte[] byteBuffer = new byte[1];

                // ASCII frame verisi '\f' (form feed) karakterine kadar okunur
                while (true) {
                    int read = in.read(byteBuffer);
                    if (read == -1) return; // Bağlantı kesildiyse çık
                    if ((char) byteBuffer[0] == '\f') break; // Frame sonu
                    frameBuilder.append((char) byteBuffer[0]);
                }

                // Frame GUI'de görüntülenir (EDT üzerinden)
                String frame = frameBuilder.toString();
                SwingUtilities.invokeLater(() -> textArea.setText(frame));

                // Bir sonraki frame için kısa bir bekleme
                Thread.sleep(50);
            }

        } catch (Exception e) {
            // Bağlantı veya diğer hatalar yakalanır
            System.err.println("Bağlantı hatası: " + e.getMessage());
        }
    }

    // GUI arayüzü oluşturulur
    private static void createAndShowGUI() {
        JFrame frame = new JFrame("ASCII Video Client");
        frame.setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
        frame.setSize(800, 600);

        // Monospace yazı tipiyle metin alanı oluşturulur
        textArea = new JTextArea();
        textArea.setFont(new java.awt.Font("Monospaced", java.awt.Font.PLAIN, 12));
        textArea.setEditable(false); // Kullanıcı düzenleyemez
        textArea.setLineWrap(false); // Satır kaydırma kapalı

        // Scroll bar eklenir ve pencereye yerleştirilir
        JScrollPane scrollPane = new JScrollPane(textArea);
        frame.add(scrollPane);

        frame.setVisible(true); // Pencere görünür hale getirilir
    }
}