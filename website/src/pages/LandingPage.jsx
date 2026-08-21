import React, { useState, useRef, useEffect } from 'react';
import Header from '../components/Header';
import { ArrowRight, ChevronDown } from 'lucide-react';
import { useNavigate } from 'react-router-dom';
import ImageComparison from '../components/ImageComparison';
import './LandingPage.css';

export default function LandingPage() {
  const [activeTab, setActiveTab] = useState('external');
  const [isDropdownOpen, setIsDropdownOpen] = useState(false);
  const [openFaq, setOpenFaq] = useState(null);
  const dropdownRef = useRef(null);
  const navigate = useNavigate();

  const faqs = [
    { q: "Как открыть меню чита?", a: "Нажмите клавишу Insert на клавиатуре для открытия/закрытия меню." },
    { q: "Как установить и запустить?", a: "1. Запустите лоадер от имени администратора. 2. Авторизуйтесь. 3. Нажмите Inject и дождитесь конца загрузки драйвера. 4. Запустите игру и зайдите на сервер. 5. Нажмите Launch — меню появится в игре." },
    { q: "Ошибка \"Failed to load driver\"", a: "1. Убедитесь, что Изоляция ядра (Core Isolation → Целостность памяти) отключена.\n2. Перезагрузите ПК после изменений.\n3. На Windows 11 25H2 выполните в CMD от администратора:\nbcdedit /set hypervisorlaunchtype off\nи перезагрузите ПК.\n4. Убедитесь, что Windows Defender выключен.\n5. Запускайте лоадер ДО запуска игры — если игра уже запущена, драйвер может быть заблокирован.\n6. Попробуйте отключить или удалить другие античиты (например, Faceit AC, Riot Vanguard), так как они могут блокировать загрузку драйвера." },
    { q: "Что будет, если выйдет обновление сервера?", a: "Наша команда оперативно обновляет софт под новые патчи, обычно это занимает от нескольких часов до суток." },
    { q: "Безопасно ли использовать?", a: "Да, чит работает через kernel-level драйвер и не внедряется в процесс игры. External подход + streamproof обеспечивают максимальную безопасность." },
    { q: "Как получить техническую поддержку?", a: "Вы можете обратиться напрямую к владельцу - t.me/fraud444" },
  ];

  const toggleFaq = (index) => {
    setOpenFaq(openFaq === index ? null : index);
  };

  useEffect(() => {
    function handleClickOutside(event) {
      if (dropdownRef.current && !dropdownRef.current.contains(event.target)) {
        setIsDropdownOpen(false);
      }
    }
    document.addEventListener("mousedown", handleClickOutside);
    return () => document.removeEventListener("mousedown", handleClickOutside);
  }, [dropdownRef]);

  return (
    <div className="landing-page">
      <Header />

      {/* Background with subtle circles */}
      <div className="hero-bg-circle"></div>

      {/* Hero Section */}
      <section className="hero-section">
        <div className="container hero-container">

          <div className="hero-content">
            <h1 className="hero-title">Ваш центр<br />возможностей.</h1>
            <p className="hero-subtitle">
              Приватные внешние модификации для GTA V. <br />
              Максимальная безопасность и регулярные обновления.
            </p>
            <div className="hero-actions" ref={dropdownRef} style={{ position: 'relative', zIndex: 100 }}>
              <button
                className="btn-secondary"
                style={{ padding: '0 32px', height: '48px', fontSize: '16px', display: 'flex', alignItems: 'center', gap: '8px' }}
                onClick={() => setIsDropdownOpen(!isDropdownOpen)}
              >
                Наши продукты <ArrowRight size={18} />
              </button>

              {isDropdownOpen && (
                <div className="hero-products-dropdown products-dropdown">
                  <div className="products-grid">
                    <div className="product-dropdown-card" onClick={(e) => { e.stopPropagation(); navigate('/games/external'); }}>
                      <span className="product-icon"><img src="/external.png" style={{ width: '24px', height: '24px', objectFit: 'contain', transform: 'scale(1.7)' }} alt="EXTERNAL" /></span>
                      <span className="product-name" style={{ textAlign: 'left', flex: 1, display: 'flex', flexDirection: 'column' }}>
                        <span>EXTERNAL</span>
                        <span style={{ fontSize: '12px', fontWeight: '400' }}></span>
                      </span>
                      <span className="product-status">Undetected</span>
                    </div>
                    <div className="product-dropdown-card" onClick={(e) => { e.stopPropagation(); navigate('/games/trigger'); }}>
                      <span className="product-icon"><img src="/external.png" style={{ width: '24px', height: '24px', objectFit: 'contain', transform: 'scale(1.7)' }} alt="TRIGGER" /></span>
                      <span className="product-name" style={{ textAlign: 'left', flex: 1, display: 'flex', flexDirection: 'column' }}>
                        <span>TRIGGER</span>
                        <span style={{ fontSize: '12px', fontWeight: '400' }}></span>
                      </span>
                      <span className="product-status">Undetected</span>
                    </div>
                  </div>
                </div>
              )}
            </div>
          </div>

          {/* Product Showcase */}
          <div className="product-showcase-wrapper">
            <img
              src="/macbook.png"
              alt="Macbook Preview"
              style={{ width: '100%', maxWidth: '700px', height: 'auto', objectFit: 'contain', filter: 'drop-shadow(0 24px 48px rgba(0,0,0,0.4))' }}
            />
          </div>

        </div>
      </section>

      {/* Comparison Section */}
      <section className="comparison-section">
        <div className="container">
          <div className="comparison-header">
            <h2 className="comparison-title">Скрытие чита на записи</h2>
            <p className="comparison-subtitle">OBS, Nvidia, Discord and Other</p>
          </div>

          <div className="comparison-slider-wrapper">
            <ImageComparison
              beforeImage="/comparison-without.png"
              afterImage="/comparison-with.png"
            />
          </div>
        </div>
      </section>



      {/* New Features Showcase Section */}
      <section className="new-features-showcase" style={{ position: 'relative', zIndex: 1, padding: '40px 0 80px 0' }}>
        <div className="container" style={{ display: 'flex', justifyContent: 'center', gap: '24px', alignItems: 'stretch', flexWrap: 'wrap' }}>
          
          {/* Left Block */}
          <div style={{ flex: '1', minWidth: '300px', maxWidth: '350px', background: 'rgba(255,255,255,0.03)', border: '1px solid rgba(255,255,255,0.07)', borderRadius: '24px', padding: '40px', display: 'flex', flexDirection: 'column', gap: '24px', position: 'relative', overflow: 'hidden' }}>
            <div style={{ position: 'relative', zIndex: 2 }}>
              <h2 style={{ color: 'white', fontSize: '36px', fontWeight: '800', margin: '0 0 16px 0', letterSpacing: '-0.02em', lineHeight: '1.1' }}>
                Безопасность
              </h2>
              <p style={{ color: 'rgba(255,255,255,0.6)', fontSize: '18px', lineHeight: '1.6', margin: 0 }}>
                0 банов за более чем 6 месяцев тестов external продукта.
              </p>
            </div>
            <div style={{ flex: 1, display: 'flex', alignItems: 'flex-end', justifyContent: 'center', position: 'relative', zIndex: 2, marginTop: '20px' }}>
              <img src="/image 39.svg" alt="Invisible" style={{ width: '100%', maxWidth: '290px', objectFit: 'contain' }} />
            </div>
          </div>

          {/* Right Block */}
          <div style={{ flex: '2', minWidth: '320px', maxWidth: '750px', background: 'rgba(255,255,255,0.03)', border: '1px solid rgba(255,255,255,0.07)', borderRadius: '24px', padding: '40px', display: 'flex', flexDirection: 'column', gap: '24px', position: 'relative', overflow: 'hidden' }}>
            <div style={{ position: 'relative', zIndex: 2 }}>
              <h2 style={{ color: 'white', fontSize: '36px', fontWeight: '800', margin: '0 0 16px 0', letterSpacing: '-0.02em', lineHeight: '1.1' }}>
                Почуствуй разницу!
              </h2>
              <p style={{ color: 'rgba(255,255,255,0.6)', fontSize: '18px', lineHeight: '1.6', margin: 0, maxWidth: '500px' }}>
                Полная кастомизация и удобство для каждого элемента, фактор удобства продукта превыше всего!
              </p>
            </div>
            <div style={{ flex: 1, display: 'flex', alignItems: 'flex-end', justifyContent: 'flex-start', position: 'relative', zIndex: 2, marginTop: '20px' }}>
              <img src="/11111.png" alt="Customization" style={{ width: '100%', maxWidth: '100%', objectFit: 'contain', borderRadius: '12px' }} />
            </div>
          </div>

        </div>
      </section>

      {/* FAQ Section */}
      <section className="faq-section">
        <div className="container">
          <h2 className="faq-main-title">Часто задаваемые вопросы</h2>
          <div className="faq-columns">

            {/* Left Column */}
            <div className="faq-column">
              {faqs.filter((_, i) => i % 2 === 0).map((faq, index) => {
                const actualIndex = index * 2;
                return (
                  <div
                    key={actualIndex}
                    className={`faq-item ${openFaq === actualIndex ? 'active' : ''}`}
                    onClick={() => toggleFaq(actualIndex)}
                  >
                    <div className="faq-header">
                      <h3 className="faq-question">{faq.q}</h3>
                      <ChevronDown className="faq-icon" size={20} />
                    </div>
                    <div className="faq-answer-wrapper">
                      <div className="faq-answer">
                        <div className="faq-answer-inner" style={{ whiteSpace: 'pre-wrap' }}>
                          <p style={{ margin: 0 }}>{faq.a}</p>
                        </div>
                      </div>
                    </div>
                  </div>
                );
              })}
            </div>

            {/* Right Column */}
            <div className="faq-column">
              {faqs.filter((_, i) => i % 2 !== 0).map((faq, index) => {
                const actualIndex = index * 2 + 1;
                return (
                  <div
                    key={actualIndex}
                    className={`faq-item ${openFaq === actualIndex ? 'active' : ''}`}
                    onClick={() => toggleFaq(actualIndex)}
                  >
                    <div className="faq-header">
                      <h3 className="faq-question">{faq.q}</h3>
                      <ChevronDown className="faq-icon" size={20} />
                    </div>
                    <div className="faq-answer-wrapper">
                      <div className="faq-answer">
                        <div className="faq-answer-inner" style={{ whiteSpace: 'pre-wrap' }}>
                          <p style={{ margin: 0 }}>{faq.a}</p>
                        </div>
                      </div>
                    </div>
                  </div>
                );
              })}
            </div>

          </div>
        </div>
      </section>
      {/* Footer */}
      <footer style={{
        borderTop: '1px solid rgba(255,255,255,0.05)',
        padding: '32px 0',
        marginTop: '60px',
        color: 'rgba(255,255,255,0.5)',
        fontSize: '14px'
      }}>
        <div className="container" style={{
          display: 'flex',
          justifyContent: 'center',
          alignItems: 'center',
          gap: '24px',
          flexWrap: 'wrap'
        }}>
          <div>© 2026 shadow. Все права защищены.</div>
          <div style={{ width: '1px', height: '16px', background: 'rgba(255,255,255,0.1)' }}></div>
          <div style={{ display: 'flex', gap: '16px' }}>
            <a href="#" style={{ color: 'inherit', textDecoration: 'none', transition: 'color 0.2s' }} onMouseOver={e => e.target.style.color = 'white'} onMouseOut={e => e.target.style.color = 'rgba(255,255,255,0.5)'}>Пользовательское соглашение</a>
            <a href="#" style={{ color: 'inherit', textDecoration: 'none', transition: 'color 0.2s' }} onMouseOver={e => e.target.style.color = 'white'} onMouseOut={e => e.target.style.color = 'rgba(255,255,255,0.5)'}>Политика конфиденциальности</a>
          </div>
          <div style={{ width: '1px', height: '16px', background: 'rgba(255,255,255,0.1)' }}></div>
          <div style={{ display: 'flex', gap: '12px' }}>
            <a href="#" style={{ display: 'flex', alignItems: 'center', gap: '6px', background: 'rgba(255,255,255,0.05)', padding: '6px 12px', borderRadius: '6px', color: 'inherit', textDecoration: 'none', transition: 'background 0.2s' }} onMouseOver={e => e.currentTarget.style.background = 'rgba(255,255,255,0.1)'} onMouseOut={e => e.currentTarget.style.background = 'rgba(255,255,255,0.05)'}>
              <svg width="14" height="14" viewBox="0 0 24 24" fill="currentColor"><path d="M20.317 4.3698a19.7913 19.7913 0 00-4.8851-1.5152.0741.0741 0 00-.0785.0371c-.211.3753-.4447.8648-.6083 1.2495-1.8447-.2762-3.68-.2762-5.4868 0-.1636-.3933-.4058-.8742-.6177-1.2495a.077.077 0 00-.0785-.037 19.7363 19.7363 0 00-4.8852 1.515.0699.0699 0 00-.0321.0277C.5334 9.0458-.319 13.5799.0992 18.0578a.0824.0824 0 00.0312.0561c2.0528 1.5076 4.0413 2.4228 5.9929 3.0294a.0777.0777 0 00.0842-.0276c.4616-.6304.8731-1.2952 1.226-1.9942a.076.076 0 00-.0416-.1057c-.6528-.2476-1.2743-.5495-1.8722-.8923a.077.077 0 01-.0076-.1277c.1258-.0943.2517-.1923.3718-.2914a.0743.0743 0 01.0776-.0105c3.9278 1.7933 8.18 1.7933 12.0614 0a.0739.0739 0 01.0785.0095c.1202.099.246.1981.3728.2924a.077.077 0 01-.0066.1276 12.2986 12.2986 0 01-1.873.8914.0766.0766 0 00-.0407.1067c.3604.698.7719 1.3628 1.225 1.9932a.076.076 0 00.0842.0286c1.961-.6067 3.9495-1.5219 6.0023-3.0294a.077.077 0 00.0313-.0552c.5004-5.177-.8382-9.6739-3.5485-13.6604a.061.061 0 00-.0312-.0286zM8.02 15.3312c-1.1825 0-2.1569-1.0857-2.1569-2.419 0-1.3332.9555-2.4189 2.157-2.4189 1.2108 0 2.1757 1.0952 2.1568 2.419 0 1.3332-.9555 2.4189-2.1569 2.4189zm7.9748 0c-1.1825 0-2.1569-1.0857-2.1569-2.419 0-1.3332.9554-2.4189 2.1569-2.4189 1.2108 0 2.1757 1.0952 2.1568 2.419 0 1.3332-.946 2.4189-2.1568 2.4189Z" /></svg>
              Discord
            </a>
            <a href="#" style={{ display: 'flex', alignItems: 'center', gap: '6px', background: 'rgba(255,255,255,0.05)', padding: '6px 12px', borderRadius: '6px', color: 'inherit', textDecoration: 'none', transition: 'background 0.2s' }} onMouseOver={e => e.currentTarget.style.background = 'rgba(255,255,255,0.1)'} onMouseOut={e => e.currentTarget.style.background = 'rgba(255,255,255,0.05)'}>
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M22 2L11 13"></path><path d="M22 2L15 22L11 13L2 9L22 2Z"></path></svg>
              Telegram
            </a>
            <a href="#" style={{ display: 'flex', alignItems: 'center', gap: '6px', background: 'rgba(255,255,255,0.05)', padding: '6px 12px', borderRadius: '6px', color: 'inherit', textDecoration: 'none', transition: 'background 0.2s' }} onMouseOver={e => e.currentTarget.style.background = 'rgba(255,255,255,0.1)'} onMouseOut={e => e.currentTarget.style.background = 'rgba(255,255,255,0.05)'}>
              <svg width="14" height="14" viewBox="0 0 24 24" fill="currentColor" style={{ transform: 'scale(1.2)' }}><path d="M23.498 6.186a3.016 3.016 0 0 0-2.122-2.136C19.505 3.545 12 3.545 12 3.545s-7.505 0-9.377.505A3.017 3.017 0 0 0 .502 6.186C0 8.07 0 12 0 12s0 3.93.502 5.814a3.016 3.016 0 0 0 2.122 2.136c1.871.505 9.376.505 9.376.505s7.505 0 9.377-.505a3.015 3.015 0 0 0 2.122-2.136C24 15.93 24 12 24 12s0-3.93-.502-5.814zM9.545 15.568V8.432L15.818 12l-6.273 3.568z" /></svg>
              YouTube
            </a>
          </div>
        </div>
      </footer>
    </div>
  );
}
