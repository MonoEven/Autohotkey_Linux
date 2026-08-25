import java.awt.BorderLayout;
import java.awt.GridLayout;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import javax.swing.*;
import javax.swing.event.DocumentEvent;
import javax.swing.event.DocumentListener;

public final class JavaAtspiProbe {
    private static void marker(String name, String value) {
        try {
            Files.writeString(Path.of("/tmp/java_atspi_" + name), value + "\n",
                StandardOpenOption.CREATE, StandardOpenOption.TRUNCATE_EXISTING);
        } catch (Exception error) {
            error.printStackTrace();
        }
    }

    private static void accessible(JComponent component, String name) {
        component.setName(name);
        component.getAccessibleContext().setAccessibleName(name);
        component.getAccessibleContext().setAccessibleDescription("AHK Java oracle " + name);
    }

    private static void createUi() {
        String title = "AHK Java Probe " + ProcessHandle.current().pid();
        JFrame frame = new JFrame(title);
        frame.getAccessibleContext().setAccessibleName(title);
        frame.setDefaultCloseOperation(WindowConstants.EXIT_ON_CLOSE);

        JPanel controls = new JPanel(new GridLayout(0, 1, 4, 4));
        JTextField entry = new JTextField("Java-世界");
        accessible(entry, "JAVA-ENTRY");
        entry.getDocument().addDocumentListener(new DocumentListener() {
            private void changed() { marker("text", entry.getText()); }
            public void insertUpdate(DocumentEvent event) { changed(); }
            public void removeUpdate(DocumentEvent event) { changed(); }
            public void changedUpdate(DocumentEvent event) { changed(); }
        });
        controls.add(entry);

        JButton button = new JButton("Invoke Java Action");
        accessible(button, "JAVA-BUTTON");
        button.addActionListener(event -> marker("button", "clicked"));
        controls.add(button);

        JList<String> list = new JList<>(new String[] {"Alpha", "Bravo", "世界"});
        list.setSelectionMode(ListSelectionModel.SINGLE_SELECTION);
        accessible(list, "JAVA-LIST");
        list.addListSelectionListener(event -> {
            if (!event.getValueIsAdjusting() && list.getSelectedValue() != null)
                marker("selection", list.getSelectedValue());
        });
        controls.add(new JScrollPane(list));

        JSlider slider = new JSlider(0, 100, 25);
        accessible(slider, "JAVA-SLIDER");
        slider.addChangeListener(event -> {
            if (!slider.getValueIsAdjusting()) marker("value", Integer.toString(slider.getValue()));
        });
        controls.add(slider);

        frame.add(controls, BorderLayout.CENTER);
        frame.setSize(440, 420);
        frame.setLocationByPlatform(true);
        frame.setVisible(true);
        marker("title", title);
        marker("ready", "ready");
        new Timer(300000, event -> System.exit(0)).start();
    }

    public static void main(String[] args) throws Exception {
        SwingUtilities.invokeAndWait(JavaAtspiProbe::createUi);
    }
}
